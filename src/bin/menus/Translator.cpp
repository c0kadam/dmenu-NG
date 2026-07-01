#include "Translator.h"

#include <fstream>
#include <sstream>
#include <algorithm>

namespace
{
	std::string trim(const std::string& s)
	{
		auto begin = std::find_if_not(s.begin(), s.end(), [](unsigned char ch) { return std::isspace(ch); });
		auto end = std::find_if_not(s.rbegin(), s.rend(), [](unsigned char ch) { return std::isspace(ch); }).base();
		if (begin >= end) {
			return {};
		}
		return std::string(begin, end);
	}
}

void Translator::ReLoadTranslations()
{
	_translations.clear();
	_customLoaded = false;
}

void Translator::LoadCustomTranslations()
{
	if (_customLoaded) {
		return;
	}
	_customLoaded = true;

	// Simple, plugin-local translation file in UTF-8:
	// Data\SKSE\Plugins\dMenu\translations.txt
	// Format per line: key[= or TAB]value
	const std::string path = "Data\\SKSE\\Plugins\\dMenu\\translations.txt";
	std::ifstream file(path);
	if (!file.is_open()) {
		INFO("Custom translation file not found: {}", path);
		return;
	}

	INFO("Loading custom translations from {}", path);
	std::string line;
	while (std::getline(file, line)) {
		if (line.empty()) {
			continue;
		}
		if (line[0] == '#' || line[0] == ';') {
			continue;
		}

		auto pos = line.find_first_of("=\t");
		if (pos == std::string::npos) {
			continue;
		}

		std::string key = trim(line.substr(0, pos));
		std::string value = trim(line.substr(pos + 1));

		if (key.empty() || value.empty()) {
			continue;
		}

		// We use bare keys in code (e.g. "tab_trainer"), so allow
		// the file to optionally include a leading '$' like SKSE.
		if (!key.empty() && key[0] == '$') {
			key.erase(key.begin());
		}

		_translations[key] = { true, value };
	}
}

const char* Translator::Translate(std::string id)
{
	if (id.empty()) {
		return nullptr;
	}

	auto it = _translations.find(id);
	if (it != _translations.end()) {
		return it->second.first ? it->second.second.c_str() : nullptr;
	}

	// Load custom file on first use.
	LoadCustomTranslations();

	it = _translations.find(id);
	if (it != _translations.end() && it->second.first) {
		return it->second.second.c_str();
	}

	// Fallback to SKSE translations (Interface\Translations\dmenu_<LANG>.txt)
	std::string skseKey = id;
	if (!skseKey.empty() && skseKey[0] != '$') {
		skseKey.insert(skseKey.begin(), '$');
	}

	std::string res;
	bool ok = SKSE::Translation::Translate(skseKey, res);
	bool hasTranslation = ok && !res.empty();

	_translations[id] = { hasTranslation, res };
	return hasTranslation ? _translations[id].second.c_str() : nullptr;
}


Translatable::Translatable(std::string def, std::string key)
{
	this->def = def;
	this->key = key;
}

Translatable::Translatable(std::string def)
{
	this->def = def;
	this->key = "";
}
Translatable::Translatable()
{
	this->def = "";
	this->key = "";
}

const char* Translatable::get() const
{
	const char* ret = Translator::Translate(key);
	return ret ? ret : def.c_str();
}

bool Translatable::empty()
{
	return def.empty() && key.empty();
}
