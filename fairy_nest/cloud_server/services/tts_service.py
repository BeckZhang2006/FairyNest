"""
Text-to-Speech Service
=======================
Supports multiple TTS engines:
- Edge-TTS (Microsoft Edge, free, high quality)
- gTTS (Google Translate TTS)
- Baidu TTS API

Features:
- Chinese and English voice synthesis
- Adjustable speed and pitch
- Audio format conversion
"""

import os
import logging
import asyncio
import tempfile
from pathlib import Path
from typing import Optional

logger = logging.getLogger(__name__)

class TTSService:
    """Text-to-Speech service with multiple engine support"""
    
    def __init__(self, engine: str = "edge", default_voice: str = "zh-CN-XiaoxiaoNeural"):
        self.engine = engine
        self.default_voice = default_voice
        self.temp_dir = tempfile.mkdtemp(prefix="fairynest_tts_")
        
        logger.info(f"TTS Service initialized: engine={engine}, voice={default_voice}")
    
    async def synthesize(self, text: str, 
                        voice: Optional[str] = None,
                        rate: str = "+0%",
                        volume: str = "+0%") -> Optional[str]:
        """
        Synthesize text to speech
        
        Args:
            text: Text to synthesize
            voice: Voice ID (default from config)
            rate: Speech rate adjustment (+0%, +10%, -10%, etc.)
            volume: Volume adjustment
        
        Returns:
            Path to generated audio file, or None on failure
        """
        voice = voice or self.default_voice
        
        try:
            if self.engine == "edge":
                return await self._synthesize_edge(text, voice, rate, volume)
            elif self.engine == "gtts":
                return await self._synthesize_gtts(text)
            elif self.engine == "baidu":
                return await self._synthesize_baidu(text)
            else:
                logger.error(f"Unknown TTS engine: {self.engine}")
                return None
        
        except Exception as e:
            logger.error(f"TTS synthesis error: {e}")
            return None
    
    async def _synthesize_edge(self, text: str, voice: str, 
                               rate: str, volume: str) -> Optional[str]:
        """Synthesize using Edge-TTS (Microsoft)"""
        try:
            import edge_tts
            
            output_path = os.path.join(self.temp_dir, f"tts_{hash(text)}.mp3")
            
            communicate = edge_tts.Communicate(
                text=text,
                voice=voice,
                rate=rate,
                volume=volume
            )
            
            await communicate.save(output_path)
            
            # Convert MP3 to WAV for ESP32 compatibility
            wav_path = await self._convert_to_wav(output_path)
            
            logger.info(f"Edge-TTS synthesized: {wav_path}")
            return wav_path
        
        except ImportError:
            logger.error("edge-tts not installed, run: pip install edge-tts")
            return None
        except Exception as e:
            logger.error(f"Edge-TTS error: {e}")
            return None
    
    async def _synthesize_gtts(self, text: str) -> Optional[str]:
        """Synthesize using gTTS (Google)"""
        try:
            from gtts import gTTS
            
            output_path = os.path.join(self.temp_dir, f"tts_{hash(text)}.mp3")
            
            # Detect language
            lang = "zh" if any('\u4e00' <= c <= '\u9fff' for c in text) else "en"
            
            tts = gTTS(text=text, lang=lang, slow=False)
            tts.save(output_path)
            
            # Convert to WAV
            wav_path = await self._convert_to_wav(output_path)
            
            logger.info(f"gTTS synthesized: {wav_path}")
            return wav_path
        
        except ImportError:
            logger.error("gTTS not installed, run: pip install gtts")
            return None
        except Exception as e:
            logger.error(f"gTTS error: {e}")
            return None
    
    async def _synthesize_baidu(self, text: str) -> Optional[str]:
        """Synthesize using Baidu TTS API"""
        # Implementation would require Baidu API credentials
        logger.warning("Baidu TTS not yet implemented")
        return None
    
    async def _convert_to_wav(self, mp3_path: str) -> str:
        """Convert MP3 to WAV format for ESP32 compatibility"""
        try:
            from pydub import AudioSegment
            
            wav_path = mp3_path.replace(".mp3", ".wav")
            
            audio = AudioSegment.from_mp3(mp3_path)
            
            # Convert to 16kHz mono 16-bit (ESP32 compatible)
            audio = audio.set_frame_rate(16000)
            audio = audio.set_channels(1)
            audio = audio.set_sample_width(2)  # 16-bit
            
            audio.export(wav_path, format="wav")
            
            return wav_path
        
        except ImportError:
            logger.warning("pydub not installed, returning MP3")
            return mp3_path
        except Exception as e:
            logger.error(f"Audio conversion error: {e}")
            return mp3_path
    
    def get_available_voices(self) -> list:
        """Get list of available voices"""
        # Edge-TTS voices
        return [
            {"id": "zh-CN-XiaoxiaoNeural", "name": "晓晓", "lang": "zh-CN", "gender": "Female"},
            {"id": "zh-CN-XiaoyiNeural", "name": "晓伊", "lang": "zh-CN", "gender": "Female"},
            {"id": "zh-CN-YunjianNeural", "name": "云健", "lang": "zh-CN", "gender": "Male"},
            {"id": "zh-CN-YunxiNeural", "name": "云希", "lang": "zh-CN", "gender": "Male"},
            {"id": "zh-CN-YunxiaNeural", "name": "云夏", "lang": "zh-CN", "gender": "Male"},
            {"id": "en-US-AriaNeural", "name": "Aria", "lang": "en-US", "gender": "Female"},
            {"id": "en-US-GuyNeural", "name": "Guy", "lang": "en-US", "gender": "Male"},
        ]
    
    def cleanup(self):
        """Clean up temporary files"""
        import shutil
        if os.path.exists(self.temp_dir):
            shutil.rmtree(self.temp_dir)
            logger.info(f"Cleaned up TTS temp directory: {self.temp_dir}")
