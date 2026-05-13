import React, { useCallback, useEffect, useRef, useState } from 'react';
import './TelemetryDashboard.css';

interface TelemetrySummary {
  total_generations: number;
  error_count: number;
  error_rate: number;
  avg_latency_ms: number;
  p50_latency_ms: number;
  p95_latency_ms: number;
  avg_tokens_per_second: number;
}

interface TelemetryRecord {
  ts: number;
  latency_ms: number;
  tokens: number;
  tps: number;
  ok: boolean;
  error?: string;
}

interface TelemetryData {
  enabled: boolean;
  summary: TelemetrySummary;
  records: TelemetryRecord[];
}

// Runtime narrow for a wire frame → TelemetryData. Avoids the
// double-cast (`as unknown as TelemetryData`) at the read site.
function isTelemetryFrame(msg: unknown): msg is TelemetryData & { type: 'telemetry_data' } {
  if (typeof msg !== 'object' || msg === null) return false;
  const m = msg as Record<string, unknown>;
  return (
    m.type === 'telemetry_data' &&
    typeof m.enabled === 'boolean' &&
    typeof m.summary === 'object' && m.summary !== null &&
    Array.isArray(m.records)
  );
}

interface Props {
  sendMessage: (msg: string) => void;
  lastMessage: string | null;
}

function LatencyBar({ label, value, max }: { label: string; value: number; max: number }) {
  const pct = max > 0 ? Math.min(100, (value / max) * 100) : 0;
  return (
    <div className="tel-bar-row">
      <span className="tel-bar-label">{label}</span>
      <div className="tel-bar-track">
        <div className="tel-bar-fill" style={{ width: `${pct}%` }} />
      </div>
      <span className="tel-bar-val">{value.toFixed(1)}ms</span>
    </div>
  );
}

export function TelemetryDashboard({ sendMessage, lastMessage }: Props) {
  const [data, setData] = useState<TelemetryData | null>(null);
  const [enabled, setEnabled] = useState(false);
  const refreshTimer = useRef<ReturnType<typeof setInterval> | null>(null);

  const fetch = useCallback(() => {
    sendMessage(JSON.stringify({ type: 'get_telemetry' }));
  }, [sendMessage]);

  // Fetch on mount.
  useEffect(() => { fetch(); }, [fetch]);

  // Handle incoming telemetry frames.
  useEffect(() => {
    if (!lastMessage) return;
    try {
      const msg = JSON.parse(lastMessage) as Record<string, unknown>;
      if (isTelemetryFrame(msg)) {
        setData(msg);
        setEnabled(msg.enabled);
      }
    } catch { /* ignore non-telemetry frames */ }
  }, [lastMessage]);

  // Auto-refresh every 10 s while enabled.
  useEffect(() => {
    if (enabled) {
      refreshTimer.current = setInterval(fetch, 10_000);
    } else {
      if (refreshTimer.current) { clearInterval(refreshTimer.current); refreshTimer.current = null; }
    }
    return () => { if (refreshTimer.current) clearInterval(refreshTimer.current); };
  }, [enabled, fetch]);

  const handleToggle = useCallback(() => {
    const next = !enabled;
    setEnabled(next);
    sendMessage(JSON.stringify({ type: 'set_telemetry_enabled', enabled: next }));
    // Brief delay then re-fetch so the UI reflects the updated state.
    setTimeout(fetch, 200);
  }, [enabled, sendMessage, fetch]);

  const summary = data?.summary;
  const records = data?.records ?? [];
  const recentRecords = [...records].reverse().slice(0, 20);

  // Compute bar chart max from p95 (with a floor to avoid div-by-zero).
  const barMax = Math.max(summary?.p95_latency_ms ?? 0, 50);

  return (
    <div className="tel-dash">
      <div className="tel-header">
        <span className="tel-title">Telemetry</span>
        <label className="tel-toggle">
          <input type="checkbox" checked={enabled} onChange={handleToggle} />
          <span className="tel-toggle-track">
            <span className="tel-toggle-thumb" />
          </span>
          <span className="tel-toggle-label">{enabled ? 'Opt-in: ON' : 'Opt-in: OFF'}</span>
        </label>
        <button className="tel-btn" onClick={fetch}>↺</button>
      </div>

      {!enabled && (
        <div className="tel-disabled-notice">
          Telemetry is off. Enable above to record generation latency, error rates, and token timing.
          All data stays local — nothing is transmitted externally.
        </div>
      )}

      {enabled && summary && (
        <>
          <div className="tel-stats-grid">
            <div className="tel-stat">
              <span className="tel-stat-val">{summary.total_generations}</span>
              <span className="tel-stat-label">Generations</span>
            </div>
            <div className="tel-stat">
              <span className="tel-stat-val">{(summary.error_rate * 100).toFixed(1)}%</span>
              <span className="tel-stat-label">Error Rate</span>
            </div>
            <div className="tel-stat">
              <span className="tel-stat-val">{summary.avg_tokens_per_second.toFixed(1)}</span>
              <span className="tel-stat-label">Avg tok/s</span>
            </div>
          </div>

          <div className="tel-section-title">Latency</div>
          <div className="tel-bars">
            <LatencyBar label="avg" value={summary.avg_latency_ms} max={barMax} />
            <LatencyBar label="p50" value={summary.p50_latency_ms} max={barMax} />
            <LatencyBar label="p95" value={summary.p95_latency_ms} max={barMax} />
          </div>

          {recentRecords.length > 0 && (
            <>
              <div className="tel-section-title">Recent Generations</div>
              <div className="tel-records">
                {recentRecords.map((r, i) => (
                  <div key={i} className={`tel-record${r.ok ? '' : ' tel-record-err'}`}>
                    <span className="tel-record-status">{r.ok ? '✓' : '✗'}</span>
                    <span className="tel-record-lat">{r.latency_ms.toFixed(0)}ms</span>
                    <span className="tel-record-tok">{r.tokens}tok</span>
                    <span className="tel-record-tps">{r.tps.toFixed(1)}t/s</span>
                    {r.error && <span className="tel-record-errmsg">{r.error}</span>}
                  </div>
                ))}
              </div>
            </>
          )}

          {summary.total_generations === 0 && (
            <div className="tel-empty">No generations recorded yet. Generate a patch to see data.</div>
          )}
        </>
      )}
    </div>
  );
}
