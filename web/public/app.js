const SAMPLES = {
  JSON: '{\n  "name": "asvJSON++",\n  "version": "1.12.0",\n  "formats": 17,\n  "active": true,\n  "tags": ["json", "c++", "header-only"]\n}',
  JSON5: '{ // single-line comment\n  name: "asvJSON++",\n  version: 1.12,\n  active: true,\n  /* block comment */\n  tags: ["json5", "c++"],\n}',
  XML: '<root>\n  <name>asvJSON++</name>\n  <version>1.12.0</version>\n  <active>true</active>\n  <tags>\n    <item>json</item>\n    <item>c++</item>\n  </tags>\n</root>',
  YAML: 'name: asvJSON++\nversion: "1.12.0"\nactive: true\ntags:\n  - json\n  - c++\n  - header-only\n',
  CSV: 'name,version,active\nasvJSON++,1.12.0,true\n',
  TOML: 'name = "asvJSON++"\nversion = "1.12.0"\nactive = true\n\n[metadata]\nauthor = "asv"\nlicense = "MIT"\n',
  INI: '[general]\nname = asvJSON++\nversion = 1.12.0\nactive = true\n\n[metadata]\nauthor = asv\nlicense = MIT\n',
  UDE: '# UDE v1.0\nname: "asvJSON++"\nversion: 1.12.0\nactive: true\nformats: 17\ntags: [json, c++, header-only]\n',
  JSONLines: '{"name": "asvJSON++", "version": "1.12.0"}\n{"name": "sample2", "version": "2.0.0"}\n',
  Sexpr: '(root\n  (name "asvJSON++")\n  (version "1.12.0")\n  (active true)\n  (tags "json" "c++"))\n',
  TOON: 'name: asvJSON++\nactive: true\nversion: 1.12\nformats: 17\ntags: []\n  - json\n  - c++\n  - header-only\n',
  TRON: '# TRON sample\nclass Data: name, version, active, formats\nData(name="asvJSON++", version=1.12, active=true, formats=17)\n',
  GOON: 'name: asvJSON++\nactive: T\nversion: "1.12.0"\nformats: 17\ntags[]:\n  - json\n  - c++\n  - header-only\n',
  ProtobufText: '{\n  name: "asvJSON++"\n  active: true\n  version: "1.12.0"\n  formats: 17\n  tags: ["json", "c++", "header-only"]\n}\n',
};

const inputArea = document.getElementById('inputArea');
const outputArea = document.getElementById('outputArea');
const inputFormat = document.getElementById('inputFormat');
const outputFormat = document.getElementById('outputFormat');
const inputStatus = document.getElementById('inputStatus');
const outputStatus = document.getElementById('outputStatus');
const statusMsg = document.getElementById('statusMsg');
const convertBtn = document.getElementById('convertBtn');

// --- Format parsing (value may be "JSON~pretty", "JSON~minify", or "XML") ---
function parseFmt(val) {
  const parts = val.split('~');
  return { name: parts[0], pretty: parts.length > 1 ? parts[1] === 'pretty' : true };
}

function minifySample(s, fmt) {
  if (fmt === 'JSON') return s.replace(/\n\s*/g, '');
  if (fmt === 'JSON5') {
    let out = '', i = 0;
    while (i < s.length) {
      if (s[i] === '"' || s[i] === "'") {
        const quote = s[i];
        out += quote; i++;
        while (i < s.length && s[i] !== quote) {
          if (s[i] === '\\') { out += s[i] + (s[i+1] || ''); i += 2; }
          else { out += s[i]; i++; }
        }
        if (i < s.length) { out += s[i]; i++; }
      } else if (s[i] === '/' && s[i+1] === '/') {
        while (i < s.length && s[i] !== '\n') i++;
      } else if (s[i] === '/' && s[i+1] === '*') {
        i += 2;
        while (i < s.length && !(s[i] === '*' && s[i+1] === '/')) i++;
        if (i < s.length) i += 2;
      } else {
        out += s[i]; i++;
      }
    }
    return out.replace(/\n\s*/g, '');
  }
  return s;
}

let _inputIsSample = false;

// --- Samples ---
async function loadSample(target) {
  const opt = parseFmt(target === 'input' ? inputFormat.value : outputFormat.value);
  const fmt = opt.name;
  const binaryFmts = ['MessagePack', 'BSON', 'CBOR', 'Protobuf'];

  if (SAMPLES[fmt]) {
    let sample = SAMPLES[fmt];
    if (!opt.pretty) sample = minifySample(sample, fmt);
    inputArea.value = sample;
    inputStatus.textContent = `Sample loaded: ${fmt}`;
    if (target === 'input') _inputIsSample = true;
  } else if (binaryFmts.includes(fmt)) {
    inputStatus.textContent = `Generating ${fmt} sample...`;
    try {
      // Convert JSON sample to binary format via server
      const jsonSample = SAMPLES.JSON;
      const payload = JSON.stringify({ from: 'JSON', to: fmt, data: jsonSample, binary: false });
      const resp = await fetch('/api/convert', { method: 'POST', body: payload });
      const result = await resp.json();
      if (result.success && result.binary) {
        inputArea.value = result.data;
        inputStatus.textContent = `Sample loaded: ${fmt} (base64)`;
      } else {
        inputArea.value = `(Failed to generate ${fmt} sample)`;
        inputStatus.textContent = `Sample error for ${fmt}`;
      }
    } catch (err) {
      inputArea.value = `(Connection error generating ${fmt} sample)`;
      inputStatus.textContent = `Sample error: ${err.message}`;
    }
    if (target === 'input') _inputIsSample = true;
  } else {
    inputArea.value = `(Unknown format: ${fmt})`;
    inputStatus.textContent = `Sample not available for ${fmt}`;
    if (target === 'input') _inputIsSample = false;
  }
}

// Auto-reload sample on format change
inputFormat.addEventListener('change', () => {
  if (_inputIsSample) loadSample('input');
  updateFormatInfo();
});
outputFormat.addEventListener('change', updateFormatInfo);

const FORMAT_INFO = {
  JSON: 'JavaScript Object Notation - universal data interchange format',
  JSON5: 'JSON with comments, trailing commas, unquoted keys',
  XML: 'Extensible Markup Language - mixed content, attributes, namespaces',
  YAML: 'Human-readable with anchors, block scalars, multiple documents',
  CSV: 'Comma-Separated Values - tabular data (flat structure)',
  TOML: "Tom's Obvious Minimal Language - config file format",
  INI: 'Initialization file - sections with key=value pairs',
  UDE: 'Unified Data Exchange - header, multi-doc, anchors, tags',
  JSONLines: 'One JSON value per line - streaming-friendly',
  Sexpr: 'S-expressions - Lisp-style parenthesized notation',
  TOON: 'Terse Object-Oriented Notation - compact, readable',
  TRON: 'Class-based format with inheritance support',
  GOON: 'Greatly Optimized Object Notation - JSON superset with tabular arrays',
  ProtobufText: 'Human-readable Protocol Buffers',
  MessagePack: 'Binary - compact serialization, like JSON but smaller',
  BSON: 'Binary JSON - used by MongoDB, supports Date/ObjectId',
  CBOR: 'IETF binary format (RFC 7049) - IoT and COSE',
  Protobuf: 'Google binary format - sequential field numbering',
};

function updateFormatInfo() {
  const infoEl = document.getElementById('formatInfo');
  if (!infoEl) return;
  const inFmt = parseFmt(inputFormat.value).name;
  const outFmt = parseFmt(outputFormat.value).name;
  const parts = [];
  if (FORMAT_INFO[inFmt]) parts.push('In: ' + FORMAT_INFO[inFmt]);
  if (FORMAT_INFO[outFmt]) parts.push('Out: ' + FORMAT_INFO[outFmt]);
  infoEl.textContent = parts.join('  ·  ');
}
updateFormatInfo();

function clearInput() { inputArea.value = ''; inputStatus.textContent = 'Cleared'; }
function clearOutput() { outputArea.value = ''; outputStatus.textContent = 'Cleared'; }

// --- Swap formats ---
function swapFormats() {
  const tmp = inputFormat.value;
  inputFormat.value = outputFormat.value;
  outputFormat.value = tmp;
  // Also swap content
  const tc = inputArea.value;
  inputArea.value = outputArea.value;
  outputArea.value = tc;
  inputStatus.textContent = 'Formats swapped';
  outputStatus.textContent = 'Formats swapped';
  // Wrap output with binary preamble if output format is binary and no preamble yet
  const outFmt = parseFmt(outputFormat.value).name;
  const binaryFmts = ['MessagePack', 'BSON', 'CBOR', 'Protobuf'];
  if (binaryFmts.includes(outFmt) && outputArea.value.length > 0 && !outputArea.value.startsWith(_BIN_PREFIX)) {
    outputArea.value = _BIN_PREFIX + outputArea.value;
  }
}

// --- File upload ---
function loadFile(event) {
  const file = event.target.files[0];
  if (!file) return;
  const reader = new FileReader();
  reader.onload = e => {
    inputArea.value = e.target.result;
    inputStatus.textContent = `Loaded: ${file.name}`;
  };
  reader.readAsText(file);
  event.target.value = '';
}

// --- Copy output ---
function copyOutput() {
  if (!outputArea.value) return;
  navigator.clipboard.writeText(outputArea.value).then(() => {
    outputStatus.textContent = 'Copied!';
  }).catch(() => {
    outputArea.select();
    document.execCommand('copy');
    outputStatus.textContent = 'Copied!';
  });
}

// --- Download output ---
let _lastBinary = false;
const _BIN_PREFIX = '[Binary data \u2014 base64 encoded]\n\n';

function downloadOutput() {
  if (!outputArea.value) return;
  const fmtOpt = parseFmt(outputFormat.value);
  const fmt = fmtOpt.name.toLowerCase();
  const extMap = { json: 'json', json5: 'json5', xml: 'xml', yaml: 'yaml', csv: 'csv',
    toml: 'toml', ini: 'ini', ude: 'ude', jsonlines: 'jsonl', sexpr: 'sexpr',
    toon: 'toon', tron: 'tron', goon: 'goon', protobuftext: 'txt',
    messagepack: 'msgpack', bson: 'bson', cbor: 'cbor', protobuf: 'bin' };
  const ext = extMap[fmt] || 'txt';
  const isBinaryOutput = ['MessagePack', 'BSON', 'CBOR', 'Protobuf'].includes(fmtOpt.name);

  let blob;
  if (isBinaryOutput && (_lastBinary || outputArea.value.startsWith(_BIN_PREFIX))) {
    // Decode base64 to binary
    const val = outputArea.value;
    const b64 = val.startsWith(_BIN_PREFIX) ? val.slice(_BIN_PREFIX.length) : val;
    const bin = Uint8Array.from(atob(b64.trim()), c => c.charCodeAt(0));
    const mimeMap = { messagepack: 'application/msgpack', bson: 'application/bson', cbor: 'application/cbor', protobuf: 'application/x-protobuf' };
    blob = new Blob([bin], { type: mimeMap[fmt] || 'application/octet-stream' });
  } else {
    blob = new Blob([outputArea.value], { type: 'text/plain' });
  }
  const a = document.createElement('a');
  a.href = URL.createObjectURL(blob);
  a.download = `output.${ext}`;
  a.click();
  URL.revokeObjectURL(a.href);
  outputStatus.textContent = `Downloaded as output.${ext}`;
}

// --- Convert ---
async function convert() {
  const fromOpt = parseFmt(inputFormat.value);
  const toOpt = parseFmt(outputFormat.value);
  const text = inputArea.value.trim();

  if (!text) {
    setStatus('Please enter data to convert', 'error');
    return;
  }

  convertBtn.disabled = true;
  convertBtn.textContent = 'Converting...';
  setStatus('Converting...', '');

  const isBinaryInput = ['MessagePack', 'BSON', 'CBOR', 'Protobuf'].includes(fromOpt.name);

  // Strip binary preamble if present (e.g., "[Binary data — base64 encoded]\n\n...")
  let data = text;
  if (isBinaryInput && text.startsWith('[Binary data')) {
    const idx = text.indexOf('\n\n');
    if (idx >= 0) data = text.slice(idx + 2).trim();
  }

  try {
    const payload = JSON.stringify({ from: fromOpt.name, to: toOpt.name, data: data, binary: isBinaryInput, pretty: toOpt.pretty });
    const resp = await fetch('/api/convert', { method: 'POST', body: payload });
    const result = await resp.json();

    if (result.success) {
      _lastBinary = !!result.binary;
      if (result.binary) {
        const b64 = result.data;
        outputArea.value = _BIN_PREFIX + b64;
        outputStatus.textContent = 'Binary output (base64)';
      } else {
        outputArea.value = result.data;
        outputStatus.textContent = `Converted: ${fromOpt.name} → ${toOpt.name}`;
      }
      setStatus('Conversion successful', 'success');
    } else {
      outputArea.value = '';
      setStatus(`Error: ${result.error}`, 'error');
      outputStatus.textContent = 'Error';
    }
  } catch (err) {
    setStatus(`Connection error: ${err.message}`, 'error');
    outputStatus.textContent = 'Connection error';
  }

  convertBtn.disabled = false;
  convertBtn.textContent = 'Convert →';
}

function setStatus(msg, type) {
  statusMsg.textContent = msg;
  statusMsg.className = type;
}

// --- Keyboard shortcuts ---
document.addEventListener('keydown', e => {
  if ((e.ctrlKey || e.metaKey) && e.key === 'Enter') {
    e.preventDefault();
    convert();
  }
});

// --- Auto-detect format on paste (basic) ---
inputArea.addEventListener('paste', () => {
  _inputIsSample = false;
  setTimeout(() => {
    const val = inputArea.value.trim();
    if (!val) return;
    inputStatus.textContent = `Input: ${val.length} chars`;
  }, 100);
});

inputArea.addEventListener('input', () => {
  _inputIsSample = false;
  const len = inputArea.value.length;
  inputStatus.textContent = len > 0 ? `${len} chars` : 'Ready';
});


