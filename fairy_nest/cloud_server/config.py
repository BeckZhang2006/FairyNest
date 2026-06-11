"""
FairyNest Cloud Server Configuration
=====================================
Environment-based configuration with sensible defaults.

Required Environment Variables:
- OPENAI_API_KEY: OpenAI API key for LLM
- DEEPSEEK_API_KEY: Alternative: DeepSeek API key
- BAIDU_API_KEY / BAIDU_SECRET_KEY: Baidu Speech API credentials

Optional Environment Variables:
- PORT: Server port (default: 8080)
- HOST: Server host (default: 0.0.0.0)
- DATABASE_URL: SQLite database path
- LOG_LEVEL: Logging level
- WHISPER_MODEL: Whisper model size (tiny/base/small/medium/large)
- TTS_ENGINE: TTS engine (edge/gtts/baidu)
- LLM_PROVIDER: LLM provider (openai/deepseek/baidu)
- LLM_MODEL: Model name
"""

import os
from functools import lru_cache
from pydantic_settings import BaseSettings
from typing import Optional

class Settings(BaseSettings):
    # Server
    port: int = 8080
    host: str = "0.0.0.0"
    
    # Logging
    log_level: str = "INFO"
    
    # Database
    database_url: str = "sqlite+aiosqlite:///./fairynest.db"
    
    # Speech Recognition
    whisper_model: str = "base"  # tiny, base, small, medium, large
    
    # Baidu Speech API (alternative)
    baidu_api_key: Optional[str] = None
    baidu_secret_key: Optional[str] = None
    
    # TTS
    tts_engine: str = "edge"  # edge, gtts, baidu
    tts_voice: str = "zh-CN-XiaoxiaoNeural"
    
    # LLM
    llm_provider: str = "openai"  # openai, deepseek, baidu
    openai_api_key: Optional[str] = None
    openai_api_base: str = "https://api.openai.com/v1"
    openai_model: str = "gpt-3.5-turbo"
    
    deepseek_api_key: Optional[str] = None
    deepseek_api_base: str = "https://api.deepseek.com/v1"
    deepseek_model: str = "deepseek-chat"
    
    # Security
    api_key_header: str = "X-API-Key"
    jwt_secret: str = "fairynest-secret-key-change-in-production"
    
    class Config:
        env_file = ".env"
        env_prefix = "FAIRYNEST_"
        case_sensitive = False

@lru_cache()
def get_settings() -> Settings:
    """Get cached settings instance"""
    return Settings()

# Validate configuration
def validate_config():
    """Validate required configuration"""
    settings = get_settings()
    
    # Check LLM configuration
    if settings.llm_provider == "openai" and not settings.openai_api_key:
        print("WARNING: OPENAI_API_KEY not set - LLM features will not work")
    
    if settings.llm_provider == "deepseek" and not settings.deepseek_api_key:
        print("WARNING: DEEPSEEK_API_KEY not set - LLM features will not work")
    
    # Check Baidu credentials
    if not (settings.baidu_api_key and settings.baidu_secret_key):
        print("WARNING: Baidu API credentials not set - using Whisper for ASR")
    
    print(f"Configuration loaded:")
    print(f"  Port: {settings.port}")
    print(f"  LLM Provider: {settings.llm_provider}")
    print(f"  LLM Model: {settings.openai_model if settings.llm_provider == 'openai' else settings.deepseek_model}")
    print(f"  Whisper Model: {settings.whisper_model}")
    print(f"  TTS Engine: {settings.tts_engine}")
