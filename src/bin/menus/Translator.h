#pragma once

class Translator
{
public:
	static void ReLoadTranslations();

	// translate a string to the current language. Returns a null-pointer if the matching translation is not found.
	static const char* Translate(std::string id);

private:
	static inline std::unordered_map<std::string, std::pair<bool, std::string>> _translations; // id -> {has_translation, translated text}

	// load custom translations from disk (dmenu-specific txt file)
	static void LoadCustomTranslations();
	static inline bool _customLoaded = false;
};


struct Translatable
{
	Translatable();
	Translatable(std::string def, std::string key);
	Translatable(std::string def);
	std::string def;              // default string
	std::string key;  // key to look up in the translation file
	const char* get() const;      // get the translated string, or the default if no translation is found note the ptr points to the original data.
	bool empty();
};

// Convenience helper for translating UI strings.
// Usage: TR("settings_width_label", "Width")
// If a translation for the key exists, it is returned; otherwise `def` is used.
inline const char* TR(const char* key, const char* def)
{
	const char* translated = Translator::Translate(key);
	return translated ? translated : def;
}
