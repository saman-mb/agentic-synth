import React, { useEffect, useRef } from 'react';

interface WaveformPreviewProps {
  audioData?: Float32Array;
  sampleRate?: number;
  width?: number;
  height?: number;
  color?: string;
}

export const WaveformPreview: React.FC<WaveformPreviewProps> = ({
  audioData,
  sampleRate = 44100,
  width = 300,
  height = 80,
  color = '#4fc3f7',
}) => {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const animRef = useRef<number>(0);
  const dataRef = useRef<Float32Array>(new Float32Array(2048));

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    const draw = () => {
      ctx.clearRect(0, 0, width, height);
      ctx.strokeStyle = color;
      ctx.lineWidth = 1.5;
      ctx.beginPath();

      const data = audioData || dataRef.current;
      const len = Math.min(data.length, width * 2);
      const step = Math.max(1, Math.floor(len / width));

      // Draw center line
      ctx.strokeStyle = '#333';
      ctx.lineWidth = 1;
      ctx.beginPath();
      ctx.moveTo(0, height / 2);
      ctx.lineTo(width, height / 2);
      ctx.stroke();

      ctx.strokeStyle = color;
      ctx.lineWidth = 1.5;
      ctx.beginPath();

      for (let x = 0; x < width; x++) {
        const idx = x * step;
        if (idx >= data.length) break;
        const sample = data[idx];
        const y = (sample * 0.5 + 0.5) * height;
        if (x === 0) ctx.moveTo(x, y);
        else ctx.lineTo(x, y);
      }
      ctx.stroke();

      // Draw spectrum overlay (simple FFT magnitude bar)
      if (audioData && audioData.length > 256) {
        ctx.fillStyle = `${color}22`;
        const fftBars = 64;
        const barWidth = width / fftBars;
        for (let i = 0; i < fftBars; i++) {
          const idx = Math.floor((i / fftBars) * audioData.length);
          const mag = Math.abs(audioData[idx] || 0);
          const barHeight = Math.min(height, mag * height * 2);
          ctx.fillRect(i * barWidth, height - barHeight, barWidth - 1, barHeight);
        }
      }

      animRef.current = requestAnimationFrame(draw);
    };

    draw();
    return () => cancelAnimationFrame(animRef.current);
  }, [audioData, width, height, color]);

  // Simulate waveform for demo if no data provided
  useEffect(() => {
    if (audioData) return;
    let phase = 0;
    const generateSine = () => {
      const buf = dataRef.current;
      for (let i = 0; i < buf.length; i++) {
        buf[i] = Math.sin(phase) * 0.5
          + Math.sin(phase * 2.01) * 0.25
          + Math.sin(phase * 3.02) * 0.125;
        phase += 0.01;
      }
    };
    const interval = setInterval(generateSine, 50);
    return () => clearInterval(interval);
  }, [audioData]);

  return (
    <canvas
      ref={canvasRef}
      width={width}
      height={height}
      style={{
        background: '#0d0d1a',
        borderRadius: 8,
        border: '1px solid #222',
      }}
    />
  );
};

export default WaveformPreview;
