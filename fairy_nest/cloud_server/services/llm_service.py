"""
LLM Service
===========
Integrates with OpenAI-compatible APIs for semantic understanding.

Supported Providers:
- OpenAI (GPT-3.5/4)
- DeepSeek (deepseek-chat)
- Baidu ERNIE
- Any OpenAI-compatible API

Features:
- Intent recognition for voice commands
- Natural language alarm setting
- Contextual conversation
- Streaming responses
"""

import os
import logging
import json
from typing import Optional, List, Dict, Any, AsyncGenerator

import httpx

logger = logging.getLogger(__name__)

class LLMService:
    """Large Language Model service for semantic understanding"""
    
    def __init__(self):
        self.provider = os.environ.get("LLM_PROVIDER", "openai")
        
        # OpenAI config
        self.openai_key = os.environ.get("OPENAI_API_KEY")
        self.openai_base = os.environ.get("OPENAI_API_BASE", "https://api.openai.com/v1")
        self.openai_model = os.environ.get("LLM_MODEL", "gpt-3.5-turbo")
        
        # DeepSeek config
        self.deepseek_key = os.environ.get("DEEPSEEK_API_KEY")
        self.deepseek_base = "https://api.deepseek.com/v1"
        self.deepseek_model = "deepseek-chat"
        
        # Baidu config
        self.baidu_key = os.environ.get("BAIDU_API_KEY")
        self.baidu_secret = os.environ.get("BAIDU_SECRET_KEY")
        
        # HTTP client
        self.client = httpx.AsyncClient(timeout=30.0)
        
        logger.info(f"LLM Service initialized: provider={self.provider}, model={self._get_model()}")
    
    def _get_model(self) -> str:
        """Get current model name"""
        if self.provider == "openai":
            return self.openai_model
        elif self.provider == "deepseek":
            return self.deepseek_model
        return "unknown"
    
    def _get_api_key(self) -> Optional[str]:
        """Get API key for current provider"""
        if self.provider == "openai":
            return self.openai_key
        elif self.provider == "deepseek":
            return self.deepseek_key
        return None
    
    def _get_api_base(self) -> str:
        """Get API base URL for current provider"""
        if self.provider == "openai":
            return self.openai_base
        elif self.provider == "deepseek":
            return self.deepseek_base
        return ""
    
    async def chat(self, system_prompt: str, user_message: str,
                   temperature: float = 0.7,
                   max_tokens: int = 500) -> str:
        """
        Send chat completion request
        
        Args:
            system_prompt: System instructions
            user_message: User input
            temperature: Creativity (0-1)
            max_tokens: Max response length
        
        Returns:
            LLM response text
        """
        api_key = self._get_api_key()
        if not api_key:
            logger.error(f"API key not configured for {self.provider}")
            return "服务暂时不可用，请稍后重试。"
        
        try:
            headers = {
                "Authorization": f"Bearer {api_key}",
                "Content-Type": "application/json"
            }
            
            payload = {
                "model": self._get_model(),
                "messages": [
                    {"role": "system", "content": system_prompt},
                    {"role": "user", "content": user_message}
                ],
                "temperature": temperature,
                "max_tokens": max_tokens
            }
            
            url = f"{self._get_api_base()}/chat/completions"
            
            logger.debug(f"Sending request to {url}")
            
            response = await self.client.post(url, json=payload, headers=headers)
            response.raise_for_status()
            
            data = response.json()
            
            if "choices" in data and len(data["choices"]) > 0:
                result = data["choices"][0]["message"]["content"].strip()
                logger.info(f"LLM response: {result[:100]}...")
                return result
            else:
                logger.error(f"Unexpected LLM response: {data}")
                return "抱歉，我没有理解您的意思。"
        
        except httpx.HTTPStatusError as e:
            logger.error(f"LLM HTTP error: {e.response.status_code} - {e.response.text}")
            return "服务暂时不可用，请稍后重试。"
        except Exception as e:
            logger.error(f"LLM error: {e}")
            return "处理出错，请重试。"
    
    async def chat_stream(self, system_prompt: str, user_message: str,
                         temperature: float = 0.7) -> AsyncGenerator[str, None]:
        """
        Streaming chat completion
        
        Yields:
            Chunks of response text
        """
        api_key = self._get_api_key()
        if not api_key:
            yield "服务暂时不可用。"
            return
        
        try:
            headers = {
                "Authorization": f"Bearer {api_key}",
                "Content-Type": "application/json"
            }
            
            payload = {
                "model": self._get_model(),
                "messages": [
                    {"role": "system", "content": system_prompt},
                    {"role": "user", "content": user_message}
                ],
                "temperature": temperature,
                "stream": True
            }
            
            url = f"{self._get_api_base()}/chat/completions"
            
            async with self.client.stream("POST", url, json=payload, headers=headers) as response:
                async for line in response.aiter_lines():
                    if line.startswith("data: "):
                        data_str = line[6:]
                        if data_str == "[DONE]":
                            break
                        
                        try:
                            data = json.loads(data_str)
                            if "choices" in data:
                                delta = data["choices"][0].get("delta", {})
                                if "content" in delta:
                                    yield delta["content"]
                        except json.JSONDecodeError:
                            continue
        
        except Exception as e:
            logger.error(f"Streaming error: {e}")
            yield "处理出错。"
    
    async def parse_alarm_intent(self, text: str) -> Optional[Dict[str, Any]]:
        """
        Parse natural language alarm setting
        
        Examples:
            "明天早上7点半叫我起床" -> {"hour": 7, "minute": 30, "days": ...}
            "设置工作日闹钟8点" -> {"hour": 8, "minute": 0, "days": 0b0111110}
        
        Returns:
            Alarm dict or None if not an alarm command
        """
        system_prompt = """你是闹钟解析助手。解析用户的闹钟设置指令，返回JSON格式：
{
    "hour": 小时(0-23),
    "minute": 分钟(0-59),
    "days": 重复日(二进制,周日=1,周一=2,...),
    "label": 标签,
    "is_valid": 是否是有效的闹钟指令
}

规则：
- "工作日" = 周一到周五 (days=62, 0b0111110)
- "周末" = 周六周日 (days=65, 0b1000001)
- "每天" = 全部 (days=127, 0b1111111)
- "明天" = 明天对应的星期几
- 时间格式: 7点, 7点半, 8点15分等

只返回JSON，不要其他文字。"""
        
        response = await self.chat(system_prompt, text, temperature=0.1)
        
        try:
            # Extract JSON from response
            json_start = response.find('{')
            json_end = response.rfind('}') + 1
            if json_start >= 0 and json_end > json_start:
                json_str = response[json_start:json_end]
                result = json.loads(json_str)
                
                if result.get("is_valid", False):
                    return {
                        "hour": result.get("hour", 7),
                        "minute": result.get("minute", 0),
                        "days": result.get("days", 127),
                        "label": result.get("label", "闹钟"),
                        "enabled": True
                    }
        except Exception as e:
            logger.error(f"Failed to parse alarm intent: {e}")
        
        return None
    
    async def parse_light_intent(self, text: str) -> Optional[Dict[str, Any]]:
        """
        Parse natural language light control
        
        Examples:
            "开灯" -> {"action": "on", "brightness": 100}
            "把灯调暗一点" -> {"action": "dim", "brightness": 50}
            "关灯" -> {"action": "off", "brightness": 0}
        """
        text_lower = text.lower()
        
        if any(kw in text_lower for kw in ["关灯", "关闭", "off"]):
            return {"action": "off", "brightness": 0}
        
        if any(kw in text_lower for kw in ["开灯", "打开", "on"]):
            # Check for brightness specification
            import re
            brightness_match = re.search(r'(\d+)%?', text_lower)
            if brightness_match:
                brightness = int(brightness_match.group(1))
                return {"action": "on", "brightness": min(brightness, 100)}
            return {"action": "on", "brightness": 100}
        
        if any(kw in text_lower for kw in ["暗", "暗一点", "dim"]):
            return {"action": "dim", "brightness": 30}
        
        if any(kw in text_lower for kw in ["亮", "亮一点", "bright"]):
            return {"action": "bright", "brightness": 100}
        
        return None
    
    async def close(self):
        """Close HTTP client"""
        await self.client.aclose()
