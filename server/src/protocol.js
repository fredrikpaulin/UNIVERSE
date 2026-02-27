/**
 * protocol.js — Newline-delimited JSON over streams.
 *
 * createLineReader(readableStream) → { next() } async iterator yielding parsed JSON.
 * writeLine(writer, obj)           → writes JSON + newline.
 */

export function createLineReader(stream) {
  let buf = "";
  const queue = [];
  let waitResolve = null;
  let done = false;

  const decoder = new TextDecoder();

  (async () => {
    try {
      for await (const chunk of stream) {
        buf += typeof chunk === "string" ? chunk : decoder.decode(chunk, { stream: true });
        let nl;
        while ((nl = buf.indexOf("\n")) !== -1) {
          const line = buf.slice(0, nl).trim();
          buf = buf.slice(nl + 1);
          if (!line) continue;
          try {
            const obj = JSON.parse(line);
            if (waitResolve) { const r = waitResolve; waitResolve = null; r(obj); }
            else queue.push(obj);
          } catch (_) { /* skip malformed lines */ }
        }
      }
    } catch (_) { /* stream error */ }
    done = true;
    if (waitResolve) { const r = waitResolve; waitResolve = null; r(null); }
  })();

  return {
    next() {
      if (queue.length) return Promise.resolve(queue.shift());
      if (done) return Promise.resolve(null);
      return new Promise((r) => { waitResolve = r; });
    }
  };
}

export function writeLine(writer, obj) {
  writer.write(JSON.stringify(obj) + "\n");
}
