"""
Database Models
===============
SQLAlchemy models for FairyNest Cloud Server.

Tables:
- devices: Registered devices
- alarms: Alarm configurations
- csi_data: CSI measurement records
- voice_commands: Voice command history
- users: User accounts
"""

from datetime import datetime
from sqlalchemy import (
    Column, Integer, String, Float, Boolean, DateTime,
    Text, LargeBinary, ForeignKey, JSON, create_engine
)
from sqlalchemy.ext.declarative import declarative_base
from sqlalchemy.orm import relationship, sessionmaker

Base = declarative_base()

class Device(Base):
    """Registered device"""
    __tablename__ = "devices"
    
    id = Column(Integer, primary_key=True)
    device_id = Column(String(64), unique=True, nullable=False, index=True)
    name = Column(String(128), default="FairyNest Device")
    api_key = Column(String(256), nullable=False)
    
    # Hardware info
    mac_address = Column(String(17))
    firmware_version = Column(String(32))
    
    # Status
    is_online = Column(Boolean, default=False)
    last_seen = Column(DateTime, default=datetime.utcnow)
    wifi_rssi = Column(Integer)
    
    # CSI config
    csi_presence_threshold = Column(Float, default=12.0)
    csi_motion_threshold = Column(Float, default=25.0)
    
    # Timestamps
    created_at = Column(DateTime, default=datetime.utcnow)
    updated_at = Column(DateTime, default=datetime.utcnow, onupdate=datetime.utcnow)
    
    # Relationships
    alarms = relationship("Alarm", back_populates="device", cascade="all, delete-orphan")
    csi_records = relationship("CSIData", back_populates="device")
    voice_commands = relationship("VoiceCommand", back_populates="device")

class Alarm(Base):
    """Alarm configuration"""
    __tablename__ = "alarms"
    
    id = Column(Integer, primary_key=True)
    device_id = Column(String(64), ForeignKey("devices.device_id"), nullable=False)
    alarm_index = Column(Integer, default=0)  # 0-4
    
    enabled = Column(Boolean, default=True)
    hour = Column(Integer, default=7)
    minute = Column(Integer, default=30)
    days = Column(Integer, default=0b0111110)  # Bitmask
    label = Column(String(32), default="Alarm")
    
    # Settings
    is_smart_wake = Column(Boolean, default=True)  # Use CSI for smart wake
    fade_duration = Column(Integer, default=600)  # seconds
    snooze_duration = Column(Integer, default=300)  # seconds
    
    created_at = Column(DateTime, default=datetime.utcnow)
    updated_at = Column(DateTime, default=datetime.utcnow, onupdate=datetime.utcnow)
    
    # Relationships
    device = relationship("Device", back_populates="alarms")

class CSIData(Base):
    """CSI measurement record"""
    __tablename__ = "csi_data"
    
    id = Column(Integer, primary_key=True)
    device_id = Column(String(64), ForeignKey("devices.device_id"), nullable=False)
    
    # Measurements
    timestamp = Column(DateTime, default=datetime.utcnow)
    variance = Column(Float)
    breath_rate = Column(Float)
    presence_state = Column(String(16))  # empty, lying, sitting, moving
    breath_status = Column(String(16))  # none, detected, irregular
    rssi = Column(Integer)
    
    # Raw data (optional, for analysis)
    raw_data = Column(JSON, nullable=True)
    
    # Relationships
    device = relationship("Device", back_populates="csi_records")

class VoiceCommand(Base):
    """Voice command history"""
    __tablename__ = "voice_commands"
    
    id = Column(Integer, primary_key=True)
    device_id = Column(String(64), ForeignKey("devices.device_id"), nullable=False)
    
    # Command info
    timestamp = Column(DateTime, default=datetime.utcnow)
    recognized_text = Column(Text)
    confidence = Column(Float)
    response_text = Column(Text)
    
    # Processing
    processing_time_ms = Column(Integer)
    intent = Column(String(32))  # light, alarm, chat, etc.
    
    # Relationships
    device = relationship("Device", back_populates="voice_commands")

class User(Base):
    """User account"""
    __tablename__ = "users"
    
    id = Column(Integer, primary_key=True)
    username = Column(String(64), unique=True, nullable=False)
    email = Column(String(128), unique=True)
    hashed_password = Column(String(256), nullable=False)
    
    is_active = Column(Boolean, default=True)
    is_admin = Column(Boolean, default=False)
    
    created_at = Column(DateTime, default=datetime.utcnow)
    last_login = Column(DateTime)

# Database initialization
def init_database(db_url: str = "sqlite:///./fairynest.db"):
    """Initialize database tables"""
    engine = create_engine(db_url)
    Base.metadata.create_all(engine)
    return engine

# Session factory
SessionLocal = sessionmaker(autocommit=False, autoflush=False)

def get_db():
    """Get database session"""
    db = SessionLocal()
    try:
        yield db
    finally:
        db.close()
