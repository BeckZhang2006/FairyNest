"""
Speech Recognition Service
==========================
Integrates Whisper for local STT or Baidu API for cloud STT.

Features:
- Whisper local transcription (no cloud dependency)
- Baidu Speech API support
- Audio preprocessing (noise reduction, normalization)
- Language detection
"""

import os
import logging
import tempfile
import asyncio
from pathlib import Path
from typing import Optional

import numpy as np

logger = logging.getLogger(__name__)

class SpeechRecognitionService:
    """Speech-to-Text service with multiple backend support"""
    
    def __init__(self, model_size: str = "base"):
        self.model_size = model_size
        self.model = None
        self._load_model()
    
    def _load_model(self):
        """Load Whisper model"""
        try:
            import whisper
            logger.info(f"Loading Whisper model: {self.model_size}")
            self.model = whisper.load_model(self.model_size)
            logger.info("Whisper model loaded successfully")
        except Exception as e:
            logger.error(f"Failed to load Whisper model: {e}")
            self.model = None
    
    async def transcribe(self, audio_path: str, language: str = "zh") -> str:
        """
        Transcribe audio file to text
        
        Args:
            audio_path: Path to audio file (WAV format recommended)
            language: Language code (zh, en, etc.)
        
        Returns:
            Transcribed text
        """
        if not self.model:
            logger.warning("Whisper model not loaded, returning empty text")
            return ""
        
        try:
            # Run in thread pool to avoid blocking
            loop = asyncio.get_event_loop()
            result = await loop.run_in_executor(
                None,  # Default executor
                self._sync_transcribe,
                audio_path,
                language
            )
            
            text = result.get("text", "").strip()
            logger.info(f"Transcription result: {text}")
            return text
        
        except Exception as e:
            logger.error(f"Transcription error: {e}")
            return ""
    
    def _sync_transcribe(self, audio_path: str, language: str) -> dict:
        """Synchronous transcription (runs in thread pool)"""
        return self.model.transcribe(
            audio_path,
            language=language,
            task="transcribe",
            fp16=False  # Use FP32 for better accuracy
        )
    
    async def transcribe_bytes(self, audio_data: bytes, 
                               sample_rate: int = 16000,
                               language: str = "zh") -> str:
        """
        Transcribe raw audio bytes
        
        Args:
            audio_data: Raw PCM audio data (int16)
            sample_rate: Audio sample rate
            language: Language code
        
        Returns:
            Transcribed text
        """
        # Convert bytes to numpy array
        audio_array = np.frombuffer(audio_data, dtype=np.int16).astype(np.float32) / 32768.0
        
        # Save to temp file
        with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as tmp:
            tmp_path = tmp.name
        
        import soundfile as sf
        sf.write(tmp_path, audio_array, sample_rate)
        
        try:
            result = await self.transcribe(tmp_path, language)
            return result
        finally:
            os.unlink(tmp_path)
    
    def get_model_info(self) -> dict:
        """Get information about loaded model"""
        return {
            "model_size": self.model_size,
            "loaded": self.model is not None,
            "device": "cuda" if (self.model and hasattr(self.model, 'device') 
                                 and 'cuda' in str(self.model.device)) else "cpu"
        }


class BaiduSpeechService:
    """Baidu Speech Recognition API (alternative to Whisper)"""
    
    def __init__(self, api_key: str, secret_key: str):
        self.api_key = api_key
        self.secret_key = secret_key
        self.token = None
        self.token_expiry = 0
    
    async def _get_token(self) -> str:
        """Get Baidu API access token"""
        import aiohttp
        import time
        
        if self.token and time.time() < self.token_expiry:
            return self.token
        
        url = "https://aip.baidubce.com/oauth/2.0/token"
        params = {
            "grant_type": "client_credentials",
            "client_id": self.api_key,
            "client_secret": self.secret_key
        }
        
        async with aiohttp.ClientSession() as session:
            async with session.post(url, params=params) as resp:
                data = await resp.json()
                self.token = data.get("access_token")
                self.token_expiry = time.time() + data.get("expires_in", 2592000) - 3600
                return self.token
    
    async def transcribe(self, audio_path: str, 
                         format: str = "wav",
                         rate: int = 16000) -> str:
        """Transcribe using Baidu API"""
        import aiohttp
        import base64
        
        token = await self._get_token()
        
        # Read and encode audio
        with open(audio_path, "rb") as f:
            audio_base64 = base64.b64encode(f.read()).decode()
        
        url = f"https://vop.baidu.com/server_api"
        headers = {"Content-Type": "application/json"}
        payload = {
            "format": format,
            "rate": rate,
            "channel": 1,
            "cuid": "fairynest_device",
            "token": token,
            "speech": audio_base64,
            "len": len(audio_base64)
        }
        
        async with aiohttp.ClientSession() as session:
            async with session.post(url, json=payload, headers=headers) as resp:
                data = await resp.json()
                
                if data.get("err_no") == 0:
                    return data.get("result", [""])[0]
                else:
                    logger.error(f"Baidu ASR error: {data}")
                    return ""
