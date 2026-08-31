import React, { useState, useEffect } from 'react';

interface TelemetryData {
  totalGenerations: number;
  errorCount: number;
  latencyP50: number;
  latencyP95: number;
  latencyP99: number;
  latencySamples: number[];
}

export const TelemetryDashboard: React.FC = () => {
  const [data, setData] = useState<TelemetryData>({
    totalGenerations: 0,
    errorCount: 0,
    latencyP50: 0,
    latencyP95: 0,
    latencyP99: 0,
    latencySamples: [],
  });

  useEffect(() => {
    const fetchTelemetry = async () => {
      try {
        const resp = await fetch('/api/telemetry');
        if (resp.ok) {
          const json = await resp.json();
          setData(json);
        }
      } catch {
        // WebSocket bridge may not be connected
      }
    };
    fetchTelemetry();
    const interval = setInterval(fetchTelemetry, 5000);
    return () => clearInterval(interval);
  }, []);

  const errorRate = data.totalGenerations > 0
    ? ((data.errorCount / data.totalGenerations) * 100).toFixed(1)
    : '0.0';

  return (
    <div style={{ padding: 16, color: '#ccc', fontFamily: 'monospace', fontSize: 13 }}>
      <h3 style={{ color: '#4fc3f7', marginBottom: 12 }}>Telemetry Dashboard</h3>

      <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 12 }}>
        <MetricCard label="Generations" value={data.totalGenerations.toString()} />
        <MetricCard label="Error Rate" value={`${errorRate}%`} color={parseFloat(errorRate) > 5 ? '#f44336' : '#4caf50'} />
        <MetricCard label="P50 Latency" value={`${data.latencyP50.toFixed(1)}ms`} />
        <MetricCard label="P95 Latency" value={`${data.latencyP95.toFixed(1)}ms`} color="#ff9800" />
        <MetricCard label="P99 Latency" value={`${data.latencyP99.toFixed(1)}ms`} color="#f44336" />
        <MetricCard label="Total Errors" value={data.errorCount.toString()} />
      </div>

      {data.latencySamples.length > 0 && (
        <div style={{ marginTop: 16 }}>
          <div style={{ color: '#999', marginBottom: 4 }}>Latency Histogram</div>
          <div style={{ height: 60, display: 'flex', alignItems: 'flex-end', gap: 2 }}>
            {data.latencySamples.slice(0, 50).map((sample, i) => (
              <div key={i} style={{
                width: 4,
                height: `${Math.min(100, sample / 5)}%`,
                backgroundColor: sample > 100 ? '#f44336' : '#4fc3f7',
                borderRadius: '2px 2px 0 0',
              }} />
            ))}
          </div>
        </div>
      )}
    </div>
  );
};

const MetricCard: React.FC<{ label: string; value: string; color?: string }> = ({ label, value, color }) => (
  <div style={{ background: '#1a1a2e', borderRadius: 8, padding: '8px 12px' }}>
    <div style={{ color: '#666', fontSize: 11, marginBottom: 2 }}>{label}</div>
    <div style={{ color: color || '#fff', fontSize: 18, fontWeight: 'bold' }}>{value}</div>
  </div>
);

export default TelemetryDashboard;
