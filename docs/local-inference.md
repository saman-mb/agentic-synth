# Local LLM Inference for Agentic Synth

> **Runtime dependency, not build dependency.** Agentic Synth links no llama.cpp code. It calls `llama-server` over HTTP (`/completion`, `/embedding`). You must run `llama-server` yourself, either on the same machine or on a LAN-reachable host. The plugin will not produce patches without it.

Run a local LLM inference server on a dedicated machine (e.g. Framework Desktop with 128GB unified memory) and connect Agentic Synth running on a separate machine (e.g. Mac Mini) to it over your local network. A single-box setup (same machine running both `llama-server` and the plugin) is also supported — point `GrammarSamplerConfig::server_url` at `http://127.0.0.1:8080`.

## Architecture

```
┌─────────────────────────────┐         ┌─────────────────────────────┐
│   Mac Mini (DAW/Plugin)     │         │   Framework Desktop        │
│                             │         │   (AMD Ryzen AI Max+ 395)  │
│   Agentic Synth             │  HTTP   │                             │
│   ┌───────────────────┐     │◄───────►│   llama.cpp server          │
│   │ GrammarSampler    │─────┘         │   ┌─────────────────────┐  │
│   │ server_url:8080   │               │   │ /completion         │  │
│   └───────────────────┘               │   │ /embedding          │  │
│   ┌───────────────────┐               │   │ /health             │  │
│   │ SemanticMapper    │               │   └─────────────────────┘  │
│   │ server_url:8080   │               │   Model: Llama 70B Q4_K_M │
│   └───────────────────┘               │   Draft: Llama 3B Q4      │
│                                        │                             │
│   LAN: 192.168.x.x/Mac                │   LAN: 192.168.x.x/FW      │
└─────────────────────────────┘         └─────────────────────────────┘
```

**Two separate connections:**
1. **GrammarSampler** → `POST /completion` with GBNF grammar for structured patch generation
2. **SemanticMapper** → `POST /embedding` for semantic similarity matching

## Model Recommendations

### For Patch Generation (GrammarSampler)

| Model | Size | Quant | RAM Use | Tok/s* | Why |
|-------|------|-------|---------|--------|-----|
| **Llama 3.1 70B** | ~40GB | Q4_K_M | ~40GB | ~4-6 | Best general purpose, strong instruction following |
| **Qwen 2.5 72B** | ~41GB | Q4_K_M | ~41GB | ~4-6 | Slightly better at structured output, good alternative |
| **Mistral Large 2 123B** | ~55GB | Q3_K_M | ~55GB | ~3-4 | Best reasoning, but slower |
| **Phi-4 14B** | ~8GB | Q4_K_M | ~8GB | ~20-25 | Fast, great for quick iteration during development |
| **Llama 3.2 3B** | ~2GB | Q4_K_M | ~2GB | ~80+ | Draft model for speculative decoding only |

*\*Estimated tokens/second on Radeon 8060S (~256 GB/s bandwidth)*

### For Semantic Mapping (SemanticMapper /embedding)

| Model | Use |
|-------|-----|
| **all-MiniLM-L6-v2** (384-dim) | Default, fast, good enough for descriptor matching |
| **all-mpnet-base-v2** (768-dim) | Better accuracy, slightly slower |
| **Nomic Embed Text v1.5** (768-dim) | Strong alternative, good at domain-specific terms |

The SemanticMapper defaults to 384-dim embeddings. If you use a different model, update `embedding_dims` in `SemanticMapperConfig`.

### Running Multiple Models Simultaneously

With 128GB unified memory, you can run **several models at once**:

```
llama.cpp server --model Llama-3.1-70B-Q4_K_M.gguf    # Main: patch generation
                 --embedding                           # Enable /embedding endpoint
                 --draft-model Llama-3.2-3B-Q4_K_M.gguf # Draft: speculative decoding
```

Total RAM: ~42GB for both models. Leaves ~86GB for context — far more than you'll ever need.

## Framework Desktop Setup

### 1. Install ROCm (for GPU acceleration)

Radeon 8060S uses AMD RDNA 3.5. Install ROCm 6.x:

```bash
# Ubuntu 24.04
wget https://repo.radeon.com/amdgpu-install/6.x/ubuntu/noble/amdgpu-install_6.x_all.deb
sudo dpkg -i amdgpu-install_6.x_all.deb
sudo amdgpu-install --usecase=rocm

# Verify
rocminfo
```

If ROCm doesn't yet support the 8060S (new chip), `llama.cpp` with **CPU + BLIS** or **OpenBLAS** will still work fine — the 16 Zen 5 cores + 256 GB/s bandwidth are plenty capable.

### 2. Build llama.cpp with GPU support

```bash
git clone https://github.com/ggerganov/llama.cpp
cd llama.cpp
mkdir build && cd build

# With ROCm (HIP):
cmake .. -DCMAKE_C_COMPILER=clang-18 -DCMAKE_CXX_COMPILER=clang++-18 \
         -DLLAMA_HIPBLAS=ON -DCMAKE_PREFIX_PATH=/opt/rocm

# Without ROCm (CPU fallback, still good):
cmake .. -DLLAMA_BLAS=ON -DLLAMA_BLAS_VENDOR=OpenBLAS

make -j$(nproc)
```

### 3. Download Models

```bash
# huggingface-cli or direct download
# Llama 3.1 70B (Q4_K_M) — ~40GB download
huggingface-cli download bartowski/Meta-Llama-3.1-70B-Instruct-GGUF \
    Meta-Llama-3.1-70B-Instruct-Q4_K_M.gguf \
    --local-dir ~/models

# Llama 3.2 3B (draft model) — ~2GB
huggingface-cli download bartowski/Llama-3.2-3B-Instruct-GGUF \
    Llama-3.2-3B-Instruct-Q4_K_M.gguf \
    --local-dir ~/models

# Embedding model — ~0.5GB
huggingface-cli download nomic-ai/nomic-embed-text-v1.5-GGUF \
    nomic-embed-text-v1.5.Q4_K_M.gguf \
    --local-dir ~/models
```

### 4. Start the Inference Server

```bash
cd ~/models

./llama-server \
    --model Meta-Llama-3.1-70B-Instruct-Q4_K_M.gguf \
    --draft-model Llama-3.2-3B-Instruct-Q4_K_M.gguf \
    --embedding \
    --host 0.0.0.0 \
    --port 8080 \
    --ctx-size 8192 \
    --batch-size 512 \
    --ubatch-size 256 \
    --n-gpu-layers 99 \
    --parallel 2 \
    --cont-batching
```

**Key flags explained:**
- `--host 0.0.0.0` — listen on all network interfaces (required for the Mac Mini to reach it)
- `--embedding` — enable `/embedding` endpoint for SemanticMapper
- `--draft-model` — enable speculative decoding (~2x token throughput on the 70B)
- `--cont-batching` — allows concurrent requests (GrammarSampler + SemanticMapper can hit the server simultaneously)
- `--n-gpu-layers 99` — offload all layers to GPU (using ROCm)

### 5. Firewall

```bash
# Allow port 8080 on the Framework Desktop
sudo ufw allow 8080/tcp
```

## Network Configuration

### Find the Framework Desktop's IP

```bash
ip addr show | grep 'inet '
# Example: 192.168.1.100
```

### Verify Connection from Mac Mini

```bash
# On the Mac Mini
curl http://192.168.1.100:8080/health
# Should return: {"status": "ok"}
```

### Test Completion

```bash
curl http://192.168.1.100:8080/completion \
  -H "Content-Type: application/json" \
  -d '{
    "prompt": "<|begin_of_text|><|start_header_id|>system<|end_header_id|>You generate synth patch parameters.<|eot_id|><|start_header_id|>user<|end_header_id|>warm pad<|eot_id|><|start_header_id|>assistant<|end_header_id|>",
    "n_predict": 256,
    "temperature": 0.35,
    "grammar": "root ::= ...",
    "cache_prompt": true
  }'
```

### Test Embedding

```bash
curl http://192.168.1.100:8080/embedding \
  -H "Content-Type: application/json" \
  -d '{"content": "warm pad with slow attack"}'
```

## Configuring Agentic Synth

The `GrammarSampler` and `SemanticMapper` are configured via C++ structs at construction. Currently the `GrammarSampler` defaults to `http://127.0.0.1:8080`.

### Option A: Configure at Build Time (Recommended for Now)

Edit the default in `src/mapper/GrammarSampler.h`:

```cpp
struct GrammarSamplerConfig {
    std::string server_url{"http://192.168.1.100:8080"}; // ← your Framework Desktop IP
    // ...
};
```

Edit `src/mapper/SemanticMapper.h`:

```cpp
struct SemanticMapperConfig {
    std::string server_url{"http://192.168.1.100:8080"}; // ← enable embeddings
    int embedding_dims{768}; // match your embedding model dimensions
    // ...
};
```

Then rebuild.

### Option B: Add a Config File (Recommended Future Enhancement)

The cleanest approach is to make these configurable via a JSON file at startup without recompiling. This would involve adding a config file loader to `AgentBridge`'s constructor.

Suggested `config.json` structure:

```json
{
  "llm_server": {
    "completion_url": "http://192.168.1.100:8080",
    "embedding_url": "http://192.168.1.100:8080",
    "timeout_ms": 15000
  },
  "models": {
    "completion": "Llama 3.1 70B",
    "embedding": "nomic-embed-text-v1.5"
  }
}
```

*(This isn't implemented yet — it's a suggested enhancement.)*

## Testing the Full Pipeline

On the Mac Mini, run the agentic-synth tests that exercise the LLM integration:

```bash
# Build with the Framework Desktop IP configured
cmake -S . -B build
cmake --build build

# Run GrammarSampler tests (if they exist)
ctest --test-dir build --test-command "grammar_sampler_test"
```

Or manually test from the terminal on the Mac Mini:

```bash
# Verify the server is reachable from the Mac Mini
curl -s http://192.168.1.100:8080/health | jq .

# Time a completion request to check latency
time curl -s http://192.168.1.100:8080/completion \
  -H "Content-Type: application/json" \
  -d '{
    "prompt": "warm pad",
    "n_predict": 128,
    "temperature": 0.3
  }' | jq '.content'
```

If the server is responding but the app can't connect, check:
- **Firewall** on Framework Desktop (port 8080 open?)
- **Same subnet** (both machines on the same LAN?)
- **No VPN** interfering (some VPNs block LAN traffic)
- **`host 0.0.0.0`** set (not `127.0.0.1`)

## Performance Notes

- **Bandwidth is the bottleneck**, not compute. LPDDR5x-8000 on a 256-bit bus = ~256 GB/s. A 70B model needs ~40GB to read through once per token, giving ~6-7 tok/s theoretical max for prompt processing.
- **Decode** (generation) is memory-bandwidth-bound: ~4-6 tok/s for a 70B Q4_K_M.
- **Speculative decoding** with a 3B draft model roughly doubles generation throughput.
- **Batch processing** (prompt ingestion for embeddings) benefits from larger batches — set `--batch-size 512`.
- **Latency**: expect 15-30s for a full patch generation (256 tokens at ~6 tok/s after speculative decoding).

For comparison:

| Setup | 70B Decode | Notes |
|-------|-----------|-------|
| Framework Desktop (8060S, 256 GB/s) | ~4-6 tok/s | 128GB capacity — run anything |
| RTX 4090 (1 TB/s) | ~20-25 tok/s | Can't fit 70B (24GB VRAM) |
| 2× RTX 4090 (NVLink) | ~30-35 tok/s | Still can't fit 70B easily |
| M4 Max (400 GB/s) | ~6-8 tok/s | Max 128GB, similar profile |

The Framework Desktop trades raw speed for **capacity** — it's slower than a 4090 per token, but can run models no single consumer GPU can fit.

## Troubleshooting

| Symptom | Likely Cause | Fix |
|---------|-------------|-----|
| `Connection refused` | Server not running or wrong IP | Check `curl localhost:8080/health` on Framework Desktop, then check IP from Mac Mini |
| `Connection timeout` | Firewall blocking port 8080 | `sudo ufw allow 8080/tcp` on Framework Desktop |
| Empty response from `/completion` | Model not loaded or out of memory | Check llama.cpp logs; reduce `--ctx-size` |
| `/embedding` returns 404 | Server started without `--embedding` flag | Restart with `--embedding` |
| Slow generation (<2 tok/s) | Running on CPU, not GPU | Check `rocminfo` / build with GPU support |
| GBNF grammar errors | Grammar file not found | Check `grammar_path` in GrammarSamplerConfig |
