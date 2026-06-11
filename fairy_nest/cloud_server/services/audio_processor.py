"""
Audio Processing Service
========================
Utilities for audio preprocessing and enhancement.

Features:
- Noise reduction
- Audio normalization
- Resampling
- Voice Activity Detection (VAD)
- Audio format conversion
"""

import logging
import tempfile
from typing import Optional, Tuple

import numpy as np
import librosa
import soundfile as sf

logger = logging.getLogger(__name__)

class AudioProcessor:
    """Audio processing utilities"""
    
    def __init__(self):
        self.sample_rate = 16000
        logger.info("Audio Processor initialized")
    
    def load_audio(self, audio_path: str, target_sr: int = 16000) -> Tuple[np.ndarray, int]:
        """
        Load audio file and resample
        
        Args:
            audio_path: Path to audio file
            target_sr: Target sample rate
        
        Returns:
            Tuple of (audio_array, sample_rate)
        """
        try:
            audio, sr = librosa.load(audio_path, sr=target_sr, mono=True)
            return audio, sr
        except Exception as e:
            logger.error(f"Failed to load audio: {e}")
            # Try with soundfile as fallback
            audio, sr = sf.read(audio_path)
            if sr != target_sr:
                audio = librosa.resample(audio, orig_sr=sr, target_sr=target_sr)
            if audio.ndim > 1:
                audio = librosa.to_mono(audio.T)
            return audio, target_sr
    
    def normalize(self, audio: np.ndarray, target_db: float = -20.0) -> np.ndarray:
        """
        Normalize audio to target dB level
        
        Args:
            audio: Audio array
            target_db: Target dB (negative values)
        
        Returns:
            Normalized audio
        """
        # Calculate current RMS
        rms = np.sqrt(np.mean(audio ** 2))
        if rms == 0:
            return audio
        
        # Convert target dB to linear scale
        target_linear = 10 ** (target_db / 20)
        
        # Calculate gain
        gain = target_linear / rms
        
        # Apply gain
        normalized = audio * gain
        
        # Prevent clipping
        max_val = np.max(np.abs(normalized))
        if max_val > 1.0:
            normalized = normalized / max_val * 0.95
        
        return normalized
    
    def noise_reduction(self, audio: np.ndarray, 
                       noise_profile_duration: float = 0.5) -> np.ndarray:
        """
        Simple noise reduction using spectral gating
        
        Args:
            audio: Audio array
            noise_profile_duration: Seconds to use for noise profile
        
        Returns:
            Denoised audio
        """
        try:
            import noisereduce as nr
            
            # Estimate noise from first segment
            noise_samples = int(noise_profile_duration * self.sample_rate)
            noise_profile = audio[:noise_samples]
            
            # Apply noise reduction
            reduced = nr.reduce_noise(
                y=audio,
                y_noise=noise_profile,
                sr=self.sample_rate,
                prop_decrease=0.75
            )
            
            return reduced
        except ImportError:
            logger.warning("noisereduce not installed, skipping noise reduction")
            return audio
        except Exception as e:
            logger.error(f"Noise reduction error: {e}")
            return audio
    
    def detect_voice_activity(self, audio: np.ndarray,
                             frame_duration: float = 0.02,
                             threshold: float = 0.01) -> np.ndarray:
        """
        Simple energy-based VAD
        
        Args:
            audio: Audio array
            frame_duration: Frame size in seconds
            threshold: Energy threshold
        
        Returns:
            Boolean array of voice activity
        """
        frame_size = int(frame_duration * self.sample_rate)
        num_frames = len(audio) // frame_size
        
        vad = np.zeros(num_frames, dtype=bool)
        
        for i in range(num_frames):
            frame = audio[i * frame_size:(i + 1) * frame_size]
            energy = np.sqrt(np.mean(frame ** 2))
            vad[i] = energy > threshold
        
        return vad
    
    def trim_silence(self, audio: np.ndarray,
                    threshold: float = 0.01,
                    margin: int = 1000) -> np.ndarray:
        """
        Trim leading and trailing silence
        
        Args:
            audio: Audio array
            threshold: Energy threshold
            margin: Samples to keep around voice
        
        Returns:
            Trimmed audio
        """
        # Find non-silent regions
        energy = np.abs(audio)
        mask = energy > threshold
        
        if not np.any(mask):
            return audio
        
        # Find start and end
        start = max(0, np.argmax(mask) - margin)
        end = min(len(audio), len(audio) - np.argmax(mask[::-1]) + margin)
        
        return audio[start:end]
    
    def preprocess_for_asr(self, audio_path: str, 
                          output_path: Optional[str] = None) -> str:
        """
        Full preprocessing pipeline for ASR
        
        Args:
            audio_path: Input audio path
            output_path: Output path (auto-generated if None)
        
        Returns:
            Path to processed audio
        """
        # Load audio
        audio, sr = self.load_audio(audio_path)
        
        # Normalize
        audio = self.normalize(audio)
        
        # Noise reduction
        audio = self.noise_reduction(audio)
        
        # Trim silence
        audio = self.trim_silence(audio)
        
        # Save
        if output_path is None:
            output_path = tempfile.mktemp(suffix=".wav")
        
        sf.write(output_path, audio, sr, subtype='PCM_16')
        
        return output_path
    
    def calculate_snr(self, audio: np.ndarray) -> float:
        """
        Calculate Signal-to-Noise Ratio
        
        Args:
            audio: Audio array
        
        Returns:
            SNR in dB
        """
        # Split into frames
        frame_size = 256
        frames = librosa.util.frame(audio, frame_length=frame_size, hop_length=frame_size)
        
        # Calculate energy per frame
        energies = np.sum(frames ** 2, axis=0)
        
        # Sort and estimate noise from lowest 20%
        sorted_energies = np.sort(energies)
        noise_energy = np.mean(sorted_energies[:len(sorted_energies) // 5])
        signal_energy = np.mean(sorted_energies)
        
        if noise_energy == 0:
            return 100.0  # Very high SNR
        
        snr = 10 * np.log10(signal_energy / noise_energy)
        return snr
