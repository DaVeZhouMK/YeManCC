// Section-scoped INI edits: Layout keys in unrelated sections are not touched.
export function rtssIniValue(text: string, section: string, key: string): string | null {
  let active = false;
  for (const line of text.split(/\r?\n/)) {
    const header = line.match(/^\s*\[([^\]]+)\]\s*$/);
    if (header) { active = header[1].toLowerCase() === section.toLowerCase(); continue; }
    if (!active) continue;
    const item = line.match(/^\s*([^=;#]+?)\s*=(.*)$/);
    if (item && item[1].toLowerCase() === key.toLowerCase()) return item[2].trim();
  }
  return null;
}
export function setRtssIniValue(text: string, section: string, key: string, value: string): string {
  const lines = text.split(/\r?\n/);
  let active = false, foundSection = false, inserted = false;
  const out: string[] = [];
  for (const line of lines) {
    const header = line.match(/^\s*\[([^\]]+)\]\s*$/);
    if (header) {
      if (active && !inserted) { out.push(`${key}=${value}`); inserted = true; }
      active = header[1].toLowerCase() === section.toLowerCase();
      foundSection ||= active;
    }
    const item = active && line.match(/^\s*([^=;#]+?)\s*=/);
    if (item && item[1].toLowerCase() === key.toLowerCase()) {
      if (!inserted) { out.push(`${key}=${value}`); inserted = true; }
    } else out.push(line);
  }
  if (!foundSection) out.push(`[${section}]`);
  if (!inserted) out.push(`${key}=${value}`);
  return out.join('\r\n');
}
