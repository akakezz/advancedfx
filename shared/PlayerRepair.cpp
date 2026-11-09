#include "../deps/release/rapidxml/rapidxml.hpp"
#include "../deps/release/rapidxml/rapidxml_print.hpp"
#include "AfxMath.h"
#include "PlayerRepair.h"
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
namespace advancedfx
{
namespace
{
bool ValidMask(unsigned mask)
{
	return mask == PlayerRepair::Position || mask == PlayerRepair::Angles || mask == PlayerRepair::All;
}
double Delta(double a, double b, bool angle)
{
	double d = b - a;
	return angle ? std::remainder(d, 360.0) : d;
}
bool Number(const char *s, double &value)
{
	if (!s || !*s)
		return false;
	std::istringstream in(s);
	in.imbue(std::locale::classic());
	in >> value;
	return in && in.peek() == std::char_traits<char>::eof() && std::isfinite(value);
}
bool Integer(const char *s, int &value)
{
	double n;
	if (!Number(s, n) || n < 0 || n > INT_MAX || n != std::floor(n))
		return false;
	value = int(n);
	return true;
}
} // namespace
void PlayerRepair::Reset()
{
	auto revision = m_Revision + 1;
	*this = PlayerRepair{};
	m_Revision = revision;
}
bool PlayerRepair::Observe(int tick, const Sample &sample)
{
	if (tick < 0 || !sample.Mask || (sample.Mask & ~All) ||
	    (!m_Samples.count(tick) && m_Samples.size() >= 100000))
		return false;
	for (int c = 0; c < 5; ++c)
		if (!std::isfinite(sample.Value[c]) || std::abs(sample.Value[c]) > (c < 3 ? 1e7 : 1e6))
			return false;
	m_Samples[tick] = sample;
	++m_Revision;
	return true;
}
void PlayerRepair::Remember()
{
	++m_Revision;
	if (m_Undo.size() >= 64)
		m_Undo.erase(m_Undo.begin());
	m_Undo.push_back(m_Edits);
	m_Redo.clear();
}
bool PlayerRepair::Exclude(const Range &range)
{
	if (range.First < 0 || range.Last < range.First || !ValidMask(range.Mask) ||
	    m_Edits.Ranges.size() >= 1000)
		return false;
	for (const auto &r : m_Edits.Ranges)
		if ((r.Mask & range.Mask) && range.First <= r.Last && r.First <= range.Last)
			return false;
	Remember();
	m_Edits.Ranges.push_back(range);
	return true;
}
bool PlayerRepair::RemoveRange(size_t index)
{
	if (index >= m_Edits.Ranges.size())
		return false;
	Remember();
	m_Edits.Ranges.erase(m_Edits.Ranges.begin() + index);
	return true;
}
bool PlayerRepair::ToggleCut(int tick)
{
	if (tick < 0 || (!m_Edits.Cuts.count(tick) && m_Edits.Cuts.size() >= 10000))
		return false;
	Remember();
	if (!m_Edits.Cuts.erase(tick))
		m_Edits.Cuts.insert(tick);
	return true;
}
bool PlayerRepair::Undo(bool redo)
{
	auto &from = redo ? m_Redo : m_Undo;
	auto &to = redo ? m_Undo : m_Redo;
	if (from.empty())
		return false;
	to.push_back(m_Edits);
	m_Edits = std::move(from.back());
	from.pop_back();
	++m_Revision;
	return true;
}
bool PlayerRepair::Excluded(int tick, unsigned mask) const
{
	for (const auto &r : m_Edits.Ranges)
		if ((r.Mask & mask) && tick >= r.First && tick <= r.Last)
			return true;
	return false;
}
unsigned PlayerRepair::Evaluate(int tick, std::array<double, 5> &out, std::string *reason) const
{
	if (reason)
		reason->clear();
	unsigned result = 0;
	for (unsigned mask : {unsigned(Position), unsigned(Angles)})
	{
		const Range *range = nullptr;
		for (const auto &r : m_Edits.Ranges)
			if ((r.Mask & mask) && tick >= r.First && tick <= r.Last)
			{
				range = &r;
				break;
			}
		if (!range)
			continue;
		auto valid = [&](auto i) {
			return i != m_Samples.end() && (i->second.Mask & mask) == mask && !Excluded(i->first, mask);
		};
		auto right = m_Samples.upper_bound(range->Last);
		while (right != m_Samples.end() && !valid(right))
			++right;
		auto left = m_Samples.lower_bound(range->First);
		bool haveLeft = false;
		while (left != m_Samples.begin())
		{
			--left;
			if (valid(left))
			{
				haveLeft = true;
				break;
			}
		}
		if (!haveLeft || right == m_Samples.end())
		{
			if (reason)
				*reason = "Need valid original samples before and after the excluded range.";
			continue;
		}
		auto safe = [&](auto a, auto b) {
			if (a->second.Epoch != b->second.Epoch)
				return false;
			auto cut = m_Edits.Cuts.upper_bound(a->first);
			if (cut != m_Edits.Cuts.end() && *cut <= b->first)
				return false;
			// A recorded discontinuity anywhere inside the bridge must survive exclusion.
			for (auto i = a; i != b; ++i)
				if (i->second.Epoch != a->second.Epoch)
					return false;
			return true;
		};
		const double h = double(right->first) - left->first;
		if (h > MaxBridgeTicks || !safe(left, right))
		{
			if (reason)
				*reason = "Bridge crosses a life-state boundary, manual cut, or maximum bridge length.";
			continue;
		}
		auto before = left, after = right;
		bool haveBefore = false, haveAfter = false;
		while (before != m_Samples.begin())
		{
			--before;
			if (valid(before))
			{
				haveBefore = safe(before, left);
				break;
			}
		}
		for (++after; after != m_Samples.end(); ++after)
			if (valid(after))
			{
				haveAfter = safe(right, after);
				break;
			}
		double u = (tick - left->first) / h;
		for (int c = 0; c < 5; ++c)
			if (mask & (1u << c))
			{
				bool angle = c >= 3;
				double a = left->second.Value[c], d = Delta(a, right->second.Value[c], angle);
				if (range->Smooth)
				{
					double times[4], values[4];
					int count = 0;
					if (haveBefore)
					{
						times[count] = before->first;
						values[count++] = a - Delta(before->second.Value[c], a, angle);
					}
					int leftIndex = count;
					times[count] = left->first;
					values[count++] = a;
					times[count] = right->first;
					values[count++] = a + d;
					if (haveAfter)
					{
						times[count] = after->first;
						values[count++] =
						    a + d + Delta(right->second.Value[c], after->second.Value[c], angle);
					}
					Afx::Math::AutoTangentSettings settings;
					settings.Method = Afx::Math::AutoTangentMethod::Monotone;
					std::vector<double> slopes;
					Afx::Math::ComputeAutoTangents(times, values, count, settings, slopes);
					double v = Afx::Math::EvalWeightedHermite(u, h, a, a + d, slopes[leftIndex],
					                                          slopes[leftIndex + 1], 1, 1);
					out[c] = (std::clamp)(v, (std::min)(a, a + d), (std::max)(a, a + d));
				}
				else
					out[c] = a + u * d;
			}
		result |= mask;
	}
	return result;
}
bool PlayerRepair::Save(const wchar_t *file) const
{
	if (Demo.empty() || UserId <= 0 || MaxBridgeTicks <= 0)
		return false;
	try
	{
		rapidxml::xml_document<> doc;
		auto root = doc.allocate_node(rapidxml::node_element, "playerRepair");
		doc.append_node(root);
		auto attr = [&](auto *node, const char *key, const std::string &value) {
			node->append_attribute(
			    doc.allocate_attribute(doc.allocate_string(key), doc.allocate_string(value.c_str())));
		};
		attr(root, "version", "1");
		attr(root, "demo", Demo);
		attr(root, "player", Player);
		attr(root, "user", std::to_string(UserId));
		attr(root, "maxBridge", std::to_string(MaxBridgeTicks));
		for (const auto &s : m_Samples)
		{
			auto n = doc.allocate_node(rapidxml::node_element, "sample");
			root->append_node(n);
			attr(n, "tick", std::to_string(s.first));
			attr(n, "mask", std::to_string(s.second.Mask));
			attr(n, "epoch", std::to_string(s.second.Epoch));
			for (int c = 0; c < 5; ++c)
			{
				std::ostringstream value;
				value.imbue(std::locale::classic());
				value << std::setprecision(17) << s.second.Value[c];
				attr(n, ("v" + std::to_string(c)).c_str(), value.str());
			}
		}
		for (const auto &r : m_Edits.Ranges)
		{
			auto n = doc.allocate_node(rapidxml::node_element, "exclude");
			root->append_node(n);
			attr(n, "first", std::to_string(r.First));
			attr(n, "last", std::to_string(r.Last));
			attr(n, "mask", std::to_string(r.Mask));
			attr(n, "smooth", r.Smooth ? "1" : "0");
		}
		for (int tick : m_Edits.Cuts)
		{
			auto n = doc.allocate_node(rapidxml::node_element, "cut");
			root->append_node(n);
			attr(n, "tick", std::to_string(tick));
		}
		std::string text;
		rapidxml::print(std::back_inserter(text), doc);
		std::ofstream out(std::filesystem::path(file), std::ios::binary);
		out.write(text.data(), text.size());
		out.close();
		return bool(out);
	}
	catch (const std::exception &)
	{
		return false;
	}
}
bool PlayerRepair::Load(const wchar_t *file)
{
	try
	{
		std::ifstream in(std::filesystem::path(file), std::ios::binary | std::ios::ate);
		auto length = in.tellg();
		if (length < 0 || length > 64 * 1024 * 1024)
			return false;
		std::vector<char> bytes(size_t(length) + 1, 0);
		in.seekg(0);
		if (length && !in.read(bytes.data(), length))
			return false;
		rapidxml::xml_document<> doc;
		doc.parse<0>(bytes.data());
		auto root = doc.first_node("playerRepair");
		if (!root)
			return false;
		auto text = [&](auto *n, const char *key) -> std::string {
			auto a = n->first_attribute(key);
			if (!a)
				return {};
			return std::string(a->value(), a->value_size());
		};
		auto number = [&](auto *n, const char *key, int &v) {
			auto t = text(n, key);
			return Integer(t.c_str(), v) && t.find('\0') == std::string::npos;
		};
		PlayerRepair parsed;
		if (text(root, "version") != "1" || !number(root, "user", parsed.UserId) || parsed.UserId <= 0 ||
		    !number(root, "maxBridge", parsed.MaxBridgeTicks) || parsed.MaxBridgeTicks <= 0)
			return false;
		parsed.Demo = text(root, "demo");
		parsed.Player = text(root, "player");
		if (parsed.Demo.empty() || parsed.Demo.size() > 4096 || parsed.Player.size() > 128 ||
		    parsed.Demo.find('\0') != std::string::npos || parsed.Player.find('\0') != std::string::npos)
			return false;
		for (auto n = root->first_node(); n; n = n->next_sibling())
		{
			if (!std::strcmp(n->name(), "sample"))
			{
				int tick, mask, epoch;
				Sample s;
				if (!number(n, "tick", tick) || !number(n, "mask", mask) || !number(n, "epoch", epoch) ||
				    parsed.m_Samples.count(tick))
					return false;
				s.Mask = unsigned(mask);
				s.Epoch = unsigned(epoch);
				for (int c = 0; c < 5; ++c)
				{
					auto value = text(n, ("v" + std::to_string(c)).c_str());
					if (value.find('\0') != std::string::npos || !Number(value.c_str(), s.Value[c]))
						return false;
				}
				if (!parsed.Observe(tick, s))
					return false;
			}
			else if (!std::strcmp(n->name(), "exclude"))
			{
				Range r;
				int mask, smooth;
				if (!number(n, "first", r.First) || !number(n, "last", r.Last) || !number(n, "mask", mask) ||
				    !number(n, "smooth", smooth) || smooth > 1)
					return false;
				r.Mask = unsigned(mask);
				r.Smooth = bool(smooth);
				if (!parsed.Exclude(r))
					return false;
			}
			else if (!std::strcmp(n->name(), "cut"))
			{
				int tick;
				if (!number(n, "tick", tick) || parsed.m_Edits.Cuts.size() >= 10000 ||
				    !parsed.m_Edits.Cuts.insert(tick).second)
					return false;
			}
			else
				return false;
		}
		parsed.m_Undo.clear();
		parsed.m_Redo.clear();
		parsed.m_Revision = m_Revision + 1;
		*this = std::move(parsed);
		return true;
	}
	catch (const std::exception &)
	{
		return false;
	}
}
} // namespace advancedfx
