#!/usr/bin/env python3
"""
FairyNest Cloud Server
======================
Semantic Voice Recognition Server for Smart Bedside Terminal

Features:
- WebSocket communication with ESP32-S3 devices
- Real-time audio streaming and processing
- Speech-to-Text (Whisper)
- Large Language Model integration (OpenAI-compatible API)
- Text-to-Speech (Edge-TTS / gTTS)
- Device management
- Alarm management
- CSI data logging and analysis

API Endpoints:
- WS /ws/device - WebSocket for device communication
- REST /api/devices - Device management
- REST /api/alarms - Alarm configuration
- REST /api/csi - CSI data and analysis
- REST /api/voice - Voice processing (HTTP fallback)

Usage:
    python main.py
    uvicorn main:app --host 0.0.0.0 --port 8080 --reload
"""

import os
import sys
import json
import asyncio
import logging
import tempfile
import uuid
from datetime import datetime, timedelta
from pathlib import Path
from typing import Dict, List, Optional, Set
from contextlib import asynccontextmanager

import numpy as np
import soundfile as sf
from fastapi import FastAPI, WebSocket, WebSocketDisconnect, HTTPException, Depends, UploadFile, File, Form
from fastapi.middleware.cors import CORSMiddleware
from fastapi.staticfiles import StaticFiles
from fastapi.responses import HTMLResponse, FileResponse

# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)

# ============== CONFIGURATION ==============
from config import Settings, get_settings

# ============== SERVICES ==============
try:
    from services.speech_recognition import SpeechRecognitionService
    from services.tts_service import TTSService
    from services.llm_service import LLMService
    from services.audio_processor import AudioProcessor
    SERVICES_AVAILABLE = True
except ImportError as e:
    logger.warning(f"Some services not available: {e}")
    SERVICES_AVAILABLE = False

# ============== DATABASE ==============
from database.db import init_db, get_db_session
from database.models import Device, Alarm, CSIData, VoiceCommand

# ============== MODELS ==============
from pydantic import BaseModel

class DeviceRegister(BaseModel):
    device_id: str
    name: str
    api_key: str

class AlarmConfig(BaseModel):
    index: int
    enabled: bool
    hour: int
    minute: int
    days: int  # bitmask
    label: str

class CSIThresholdUpdate(BaseModel):
    device_id: str
    threshold: float

class VoiceTextRequest(BaseModel):
    text: str
    device_id: str

class TTSRequest(BaseModel):
    text: str
    voice: Optional[str] = "zh-CN-XiaoxiaoNeural"

# ============== DEVICE MANAGER ==============
class DeviceConnection:
    """Represents an active WebSocket connection to a device"""
    
    def __init__(self, device_id: str, websocket: WebSocket):
        self.device_id = device_id
        self.websocket = websocket
        self.connected_at = datetime.now()
        self.last_seen = datetime.now()
        self.csi_data_buffer: List[dict] = []
        self.audio_buffer = bytearray()
        self.is_recording = False
        
    async def send_json(self, data: dict):
        try:
            await self.websocket.send_json(data)
        except Exception as e:
            logger.error(f"Failed to send to {self.device_id}: {e}")
    
    async def send_binary(self, data: bytes):
        try:
            await self.websocket.send_bytes(data)
        except Exception as e:
            logger.error(f"Failed to send binary to {self.device_id}: {e}")

class DeviceManager:
    """Manages all connected devices"""
    
    def __init__(self):
        self.devices: Dict[str, DeviceConnection] = {}
        self.authorized_keys: Set[str] = set()
        
    async def connect(self, device_id: str, websocket: WebSocket) -> DeviceConnection:
        conn = DeviceConnection(device_id, websocket)
        self.devices[device_id] = conn
        logger.info(f"Device connected: {device_id} (total: {len(self.devices)})")
        return conn
    
    async def disconnect(self, device_id: str):
        if device_id in self.devices:
            del self.devices[device_id]
            logger.info(f"Device disconnected: {device_id} (total: {len(self.devices)})")
    
    def get_device(self, device_id: str) -> Optional[DeviceConnection]:
        return self.devices.get(device_id)
    
    def is_authorized(self, api_key: str) -> bool:
        # In production, validate against database
        return True  # Simplified for development
    
    def get_all_devices(self) -> List[dict]:
        result = []
        for device_id, conn in self.devices.items():
            result.append({
                "device_id": device_id,
                "connected_at": conn.connected_at.isoformat(),
                "last_seen": conn.last_seen.isoformat(),
                "is_online": True
            })
        return result

device_manager = DeviceManager()

# ============== LIFESPAN ==============
@asynccontextmanager
async def lifespan(app: FastAPI):
    """Application lifespan handler"""
    logger.info("=" * 50)
    logger.info("FairyNest Cloud Server Starting...")
    logger.info("=" * 50)
    
    # Initialize database
    await init_db()
    logger.info("Database initialized")
    
    # Initialize services
    if SERVICES_AVAILABLE:
        app.state.speech_service = SpeechRecognitionService()
        app.state.tts_service = TTSService()
        app.state.llm_service = LLMService()
        app.state.audio_processor = AudioProcessor()
        logger.info("AI services initialized")
    else:
        logger.warning("AI services not available - running in limited mode")
    
    yield
    
    # Cleanup
    logger.info("Shutting down FairyNest Cloud Server...")
    # Disconnect all devices
    for device_id in list(device_manager.devices.keys()):
        await device_manager.disconnect(device_id)

# ============== FASTAPI APP ==============
app = FastAPI(
    title="FairyNest Cloud Server",
    description="Semantic Voice Recognition & Device Management for Smart Bedside Terminal",
    version="1.0.0",
    lifespan=lifespan
)

# CORS
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],  # In production, specify actual origins
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# ============== WEBSOCKET ENDPOINT ==============
@app.websocket("/ws/device")
async def device_websocket(websocket: WebSocket):
    """
    WebSocket endpoint for ESP32-S3 devices
    
    Protocol:
    1. Device sends auth message: {"type": "auth", "api_key": "...", "device_id": "..."}
    2. Server responds: {"type": "auth_result", "success": true}
    3. Device sends status updates, audio data, CSI data
    4. Server sends commands, TTS audio, configuration
    """
    await websocket.accept()
    
    device_conn: Optional[DeviceConnection] = None
    
    try:
        # Wait for authentication
        auth_msg = await websocket.receive_json()
        
        if auth_msg.get("type") != "auth":
            await websocket.send_json({
                "type": "error",
                "message": "First message must be auth"
            })
            return
        
        api_key = auth_msg.get("api_key", "")
        device_id = auth_msg.get("device_id", "")
        
        if not device_manager.is_authorized(api_key):
            await websocket.send_json({
                "type": "auth_result",
                "success": False,
                "message": "Invalid API key"
            })
            return
        
        # Authenticate and register device
        device_conn = await device_manager.connect(device_id, websocket)
        
        await websocket.send_json({
            "type": "auth_result",
            "success": True,
            "message": "Connected to FairyNest Cloud",
            "timestamp": datetime.now().isoformat()
        })
        
        logger.info(f"Device authenticated: {device_id}")
        
        # Main message loop
        while True:
            try:
                # Receive message (text or binary)
                message = await websocket.receive()
                device_conn.last_seen = datetime.now()
                
                if "text" in message:
                    # JSON message
                    await handle_text_message(device_conn, message["text"])
                elif "bytes" in message:
                    # Binary audio data
                    await handle_binary_message(device_conn, message["bytes"])
                    
            except WebSocketDisconnect:
                break
            except Exception as e:
                logger.error(f"Error handling message from {device_id}: {e}")
                await websocket.send_json({
                    "type": "error",
                    "message": str(e)
                })
    
    except WebSocketDisconnect:
        logger.info("WebSocket disconnected during auth")
    except Exception as e:
        logger.error(f"WebSocket error: {e}")
    finally:
        if device_conn:
            await device_manager.disconnect(device_conn.device_id)

async def handle_text_message(device_conn: DeviceConnection, text: str):
    """Handle text/JSON messages from device"""
    try:
        data = json.loads(text)
        msg_type = data.get("type", "")
        
        if msg_type == "device_status":
            # Device status update
            logger.debug(f"Status from {device_conn.device_id}: {data}")
            # Store in database (async)
            
        elif msg_type == "voice_event":
            # Voice recording event
            event = data.get("event", "")
            if event == "recording_started":
                device_conn.is_recording = True
                device_conn.audio_buffer = bytearray()
                logger.info(f"Recording started on {device_conn.device_id}")
                
            elif event == "recording_finished":
                device_conn.is_recording = False
                logger.info(f"Recording finished on {device_conn.device_id}, "
                          f"buffer size: {len(device_conn.audio_buffer)} bytes")
                
                # Process the complete audio
                await process_voice_command(device_conn)
                
        elif msg_type == "voice_command":
            # Pre-processed voice command (local recognition)
            text_cmd = data.get("text", "")
            logger.info(f"Voice command from {device_conn.device_id}: {text_cmd}")
            await handle_voice_text(device_conn, text_cmd)
            
        elif msg_type == "tts_request":
            # Device requesting TTS
            tts_text = data.get("text", "")
            await send_tts_to_device(device_conn, tts_text)
            
        elif msg_type == "alarm_event":
            # Alarm event from device
            event = data.get("event", "")
            logger.info(f"Alarm event from {device_conn.device_id}: {event}")
            
        elif msg_type == "csi_data":
            # CSI data batch
            csi_batch = data.get("data", [])
            device_conn.csi_data_buffer.extend(csi_batch)
            
        elif msg_type == "ping":
            await device_conn.send_json({"type": "pong"})
            
        else:
            logger.warning(f"Unknown message type: {msg_type}")
            
    except json.JSONDecodeError:
        logger.error(f"Invalid JSON: {text}")
    except Exception as e:
        logger.error(f"Error processing text message: {e}")

async def handle_binary_message(device_conn: DeviceConnection, data: bytes):
    """Handle binary audio data from device"""
    if device_conn.is_recording:
        device_conn.audio_buffer.extend(data)
        # logger.debug(f"Received {len(data)} bytes audio from {device_conn.device_id}")

async def process_voice_command(device_conn: DeviceConnection):
    """Process recorded voice audio"""
    try:
        if len(device_conn.audio_buffer) < 1000:
            logger.warning("Audio buffer too small, ignoring")
            return
        
        # Save audio to temp file
        with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as tmp:
            tmp_path = tmp.name
        
        # Convert raw PCM to WAV
        audio_array = np.frombuffer(device_conn.audio_buffer, dtype=np.int16)
        sf.write(tmp_path, audio_array, 16000, subtype='PCM_16')
        
        logger.info(f"Audio saved: {tmp_path} ({len(audio_array)} samples)")
        
        # Speech recognition
        if SERVICES_AVAILABLE:
            speech_service = app.state.speech_service
            recognized_text = await speech_service.transcribe(tmp_path)
            logger.info(f"Recognized: {recognized_text}")
        else:
            recognized_text = "语音识别服务暂不可用"
        
        # Clean up temp file
        os.unlink(tmp_path)
        
        if recognized_text:
            # Send transcription to device
            await device_conn.send_json({
                "type": "voice_result",
                "text": recognized_text,
                "confidence": 0.95
            })
            
            # Process the command
            await handle_voice_text(device_conn, recognized_text)
        else:
            await send_tts_to_device(device_conn, "抱歉，我没有听清楚，请再说一遍。")
    
    except Exception as e:
        logger.error(f"Error processing voice command: {e}")
        await send_tts_to_device(device_conn, "处理语音时出错，请重试。")

async def handle_voice_text(device_conn: DeviceConnection, text: str):
    """Handle recognized voice text - intent recognition and LLM processing"""
    try:
        # Check for local commands first
        text_lower = text.lower()
        
        # Simple intent matching
        if any(kw in text_lower for kw in ["开灯", "打开灯", "light on"]):
            await device_conn.send_json({"command": "set_light", "brightness": 100})
            await send_tts_to_device(device_conn, "已为您打开夜灯")
            return
            
        if any(kw in text_lower for kw in ["关灯", "关闭灯", "light off"]):
            await device_conn.send_json({"command": "set_light", "brightness": 0})
            await send_tts_to_device(device_conn, "已为您关闭夜灯")
            return
            
        if any(kw in text_lower for kw in ["停止闹钟", "stop alarm"]):
            await device_conn.send_json({"command": "stop_alarm"})
            await send_tts_to_device(device_conn, "闹钟已停止")
            return
            
        if any(kw in text_lower for kw in ["贪睡", "snooze", "再睡一会"]):
            await device_conn.send_json({"command": "snooze"})
            await send_tts_to_device(device_conn, "好的，5分钟后再叫您")
            return
        
        # Use LLM for complex commands
        if SERVICES_AVAILABLE:
            llm_service = app.state.llm_service
            
            # Build prompt with context
            system_prompt = """你是FairyNest智能音箱的AI助手。你控制着一个床头智能设备，具有以下功能：
1. 夜灯控制（开关、亮度调节）
2. 闹钟管理（设置、取消、贪睡）
3. 人体存在检测（自动亮灯）
4. 温柔唤醒（渐进式亮度增加）

请以简短友好的中文回复。如果需要执行设备操作，请在回复末尾添加 [ACTION:xxx] 标记。"""
            
            response = await llm_service.chat(system_prompt, text)
            
            # Check for action commands in response
            action = None
            if "[ACTION:" in response:
                action_start = response.index("[ACTION:") + 8
                action_end = response.index("]", action_start)
                action = response[action_start:action_end]
                response = response.replace(f"[ACTION:{action}]", "").strip()
            
            # Send action to device if present
            if action:
                if action == "light_on":
                    await device_conn.send_json({"command": "set_light", "brightness": 100})
                elif action == "light_off":
                    await device_conn.send_json({"command": "set_light", "brightness": 0})
                elif action == "stop_alarm":
                    await device_conn.send_json({"command": "stop_alarm"})
                elif action == "snooze":
                    await device_conn.send_json({"command": "snooze"})
            
            # Send TTS response
            await send_tts_to_device(device_conn, response)
        else:
            await send_tts_to_device(device_conn, f"收到您的指令：{text}")
    
    except Exception as e:
        logger.error(f"Error handling voice text: {e}")
        await send_tts_to_device(device_conn, "处理命令时出错，请重试。")

async def send_tts_to_device(device_conn: DeviceConnection, text: str):
    """Generate TTS audio and send to device"""
    try:
        if not SERVICES_AVAILABLE:
            logger.warning("TTS service not available")
            return
        
        logger.info(f"Generating TTS: {text}")
        tts_service = app.state.tts_service
        
        # Generate TTS audio
        audio_path = await tts_service.synthesize(text)
        
        if audio_path and os.path.exists(audio_path):
            # Read audio file
            with open(audio_path, 'rb') as f:
                audio_data = f.read()
            
            # Send TTS header
            await device_conn.send_json({
                "type": "tts_start",
                "text": text,
                "format": "pcm",
                "sample_rate": 16000
            })
            
            # Send audio data in chunks
            chunk_size = 4096
            for i in range(0, len(audio_data), chunk_size):
                chunk = audio_data[i:i + chunk_size]
                await device_conn.send_binary(chunk)
                await asyncio.sleep(0.01)  # Small delay for device processing
            
            # Send TTS end
            await device_conn.send_json({"type": "tts_end"})
            
            # Clean up temp file
            os.unlink(audio_path)
            
            logger.info(f"TTS sent: {len(audio_data)} bytes")
        else:
            logger.error("TTS generation failed")
    
    except Exception as e:
        logger.error(f"Error sending TTS: {e}")

# ============== REST API ENDPOINTS ==============

@app.get("/api/health")
async def health_check():
    """Health check endpoint"""
    return {
        "status": "ok",
        "timestamp": datetime.now().isoformat(),
        "services": SERVICES_AVAILABLE,
        "devices_online": len(device_manager.devices)
    }

@app.get("/api/devices")
async def list_devices():
    """List all connected devices"""
    return {
        "devices": device_manager.get_all_devices(),
        "count": len(device_manager.devices)
    }

@app.get("/api/devices/{device_id}")
async def get_device(device_id: str):
    """Get device details"""
    conn = device_manager.get_device(device_id)
    if not conn:
        raise HTTPException(status_code=404, detail="Device not found")
    
    return {
        "device_id": conn.device_id,
        "connected_at": conn.connected_at.isoformat(),
        "last_seen": conn.last_seen.isoformat(),
        "is_recording": conn.is_recording,
        "csi_buffer_size": len(conn.csi_data_buffer)
    }

@app.post("/api/devices/{device_id}/command")
async def send_command(device_id: str, command: dict):
    """Send command to a device"""
    conn = device_manager.get_device(device_id)
    if not conn:
        raise HTTPException(status_code=404, detail="Device not connected")
    
    await conn.send_json(command)
    return {"success": True, "command": command}

@app.get("/api/alarms/{device_id}")
async def get_alarms(device_id: str):
    """Get alarms for a device"""
    # Return sample alarms (would query from DB in production)
    return {
        "device_id": device_id,
        "alarms": [
            {"index": 0, "enabled": True, "hour": 7, "minute": 30, 
             "days": 0b0111110, "label": "工作日闹钟"},
            {"index": 1, "enabled": True, "hour": 9, "minute": 0,
             "days": 0b1000001, "label": "周末闹钟"},
        ]
    }

@app.post("/api/alarms/{device_id}")
async def set_alarm(device_id: str, alarm: AlarmConfig):
    """Set alarm for a device"""
    conn = device_manager.get_device(device_id)
    if not conn:
        raise HTTPException(status_code=404, detail="Device not connected")
    
    await conn.send_json({
        "command": "set_alarm",
        **alarm.model_dump()
    })
    
    return {"success": True, "alarm": alarm.model_dump()}

@app.get("/api/csi/{device_id}")
async def get_csi_data(device_id: str, limit: int = 100):
    """Get recent CSI data for a device"""
    conn = device_manager.get_device(device_id)
    if not conn:
        raise HTTPException(status_code=404, detail="Device not found")
    
    data = conn.csi_data_buffer[-limit:]
    return {
        "device_id": device_id,
        "count": len(data),
        "data": data
    }

@app.post("/api/csi/threshold")
async def set_csi_threshold(update: CSIThresholdUpdate):
    """Update CSI detection threshold for a device"""
    conn = device_manager.get_device(update.device_id)
    if not conn:
        raise HTTPException(status_code=404, detail="Device not connected")
    
    await conn.send_json({
        "command": "set_csi_threshold",
        "threshold": update.threshold
    })
    
    return {"success": True, "threshold": update.threshold}

@app.post("/api/voice/process")
async def process_voice_http(audio: UploadFile = File(...), device_id: str = Form("")):
    """Process voice via HTTP (fallback for devices without WebSocket)"""
    try:
        # Save uploaded audio
        with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as tmp:
            content = await audio.read()
            tmp.write(content)
            tmp_path = tmp.name
        
        # Speech recognition
        if SERVICES_AVAILABLE:
            speech_service = app.state.speech_service
            recognized_text = await speech_service.transcribe(tmp_path)
        else:
            recognized_text = "服务暂不可用"
        
        # Clean up
        os.unlink(tmp_path)
        
        return {
            "success": True,
            "text": recognized_text,
            "device_id": device_id
        }
    
    except Exception as e:
        logger.error(f"HTTP voice processing error: {e}")
        raise HTTPException(status_code=500, detail=str(e))

@app.post("/api/tts")
async def generate_tts(request: TTSRequest):
    """Generate TTS audio"""
    try:
        if not SERVICES_AVAILABLE:
            raise HTTPException(status_code=503, detail="TTS service not available")
        
        tts_service = app.state.tts_service
        audio_path = await tts_service.synthesize(request.text, voice=request.voice)
        
        if audio_path and os.path.exists(audio_path):
            return FileResponse(
                audio_path,
                media_type="audio/wav",
                filename="tts_output.wav"
            )
        else:
            raise HTTPException(status_code=500, detail="TTS generation failed")
    
    except Exception as e:
        logger.error(f"TTS error: {e}")
        raise HTTPException(status_code=500, detail=str(e))

# ============== STATIC FILES (Frontend) ==============
frontend_path = Path(__file__).parent / "static"
if frontend_path.exists():
    app.mount("/", StaticFiles(directory=frontend_path, html=True), name="static")

@app.get("/", response_class=HTMLResponse)
async def root():
    """Serve frontend"""
    index_path = frontend_path / "index.html"
    if index_path.exists():
        return FileResponse(index_path)
    return HTMLResponse("""
    <html>
    <head><title>FairyNest Cloud Server</title></head>
    <body>
        <h1>FairyNest Cloud Server</h1>
        <p>Status: Running</p>
        <p>Services: {}</p>
        <p>Devices Online: {}</p>
    </body>
    </html>
    """.format("Available" if SERVICES_AVAILABLE else "Limited", len(device_manager.devices)))

# ============== MAIN ==============
if __name__ == "__main__":
    import uvicorn
    
    port = int(os.environ.get("PORT", 8080))
    host = os.environ.get("HOST", "0.0.0.0")
    
    logger.info(f"Starting server on {host}:{port}")
    
    uvicorn.run(
        "main:app",
        host=host,
        port=port,
        reload=True,
        log_level="info"
    )
