#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>
#include <utility>

namespace advancedfx {
// Shared encoder diagnostics, written by capture workers and read by the overlay.
// Bound both text and entry count so long recordings cannot grow the UI log forever.
class RecordingLog {
public:
 struct Entry { std::string Source, Text; };
 static constexpr size_t MaxBytes = 256 * 1024, MaxEntries = 2048;
 static RecordingLog &Get() { static RecordingLog log; return log; }
 void Append(const std::string &source, const char *data, size_t size) {
  if (!data || !size) return;
  Entry entry{source.substr(source.size() > 1024 ? source.size()-1024 : 0), {}};
  if (size > 16*1024) { data += size-16*1024; size=16*1024; }
  entry.Text.assign(data,size);
  std::lock_guard<std::mutex> lock(m_Mutex);
  m_Bytes += entry.Source.size()+entry.Text.size(); m_Entries.push_back(std::move(entry));
  while (m_Bytes > MaxBytes || m_Entries.size() > MaxEntries) {
   m_Bytes -= m_Entries.front().Source.size()+m_Entries.front().Text.size(); m_Entries.pop_front();
  }
  ++m_Revision;
 }
 void Clear() {
  std::lock_guard<std::mutex> lock(m_Mutex);
  m_Entries.clear(); m_Bytes=0; ++m_Revision;
 }
 bool Read(uint64_t &revision, std::vector<Entry> &entries) {
  std::lock_guard<std::mutex> lock(m_Mutex);
  if (revision==m_Revision) return false;
  entries.assign(m_Entries.begin(),m_Entries.end()); revision=m_Revision; return true;
 }
 // Format a consistent UI snapshot, including entries outside the visible scroll area.
 // Clipboard text is NUL-terminated, so represent unexpected embedded NULs visibly.
 static std::string ClipboardText(const std::vector<Entry> &entries) {
  std::string text, source;
  auto append = [&text](const std::string &value) {
   for (char c : value) { if (c == '\0') text += "\\0"; else text += c; }
  };
  for (const auto &entry : entries) {
   if (source != entry.Source) {
    source = entry.Source;
    if (!source.empty()) { append(source); text += '\n'; }
   }
   append(entry.Text);
   if (text.empty() || text.back() != '\n') text += '\n';
  }
  return text;
 }
private:
 std::mutex m_Mutex;
 std::deque<Entry> m_Entries;
 size_t m_Bytes=0;
 uint64_t m_Revision=1;
};
}
