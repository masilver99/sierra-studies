#pragma once

#include <cctype>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace TradeGateJson
{
	inline std::string StripJsonCommentsAndTrailingCommas(const std::string& input)
	{
		std::string noComments;
		noComments.reserve(input.size());
		bool inString = false;
		bool escape = false;
		for (size_t i = 0; i < input.size(); ++i)
		{
			const char c = input[i];
			const char next = (i + 1 < input.size()) ? input[i + 1] : '\0';

			if (inString)
			{
				noComments.push_back(c);
				if (escape)
				{
					escape = false;
					continue;
				}
				if (c == '\\')
				{
					escape = true;
					continue;
				}
				if (c == '"')
					inString = false;
				continue;
			}

			if (c == '"')
			{
				inString = true;
				noComments.push_back(c);
				continue;
			}

			// line comment
			if (c == '/' && next == '/')
			{
				i += 1;
				while (i + 1 < input.size() && input[i + 1] != '\n')
					i += 1;
				continue;
			}
			// block comment
			if (c == '/' && next == '*')
			{
				i += 1;
				while (i + 1 < input.size())
				{
					if (input[i] == '*' && input[i + 1] == '/')
					{
						i += 1;
						break;
					}
					i += 1;
				}
				continue;
			}

			noComments.push_back(c);
		}

		// remove trailing commas before } or ] (while respecting strings)
		std::string out;
		out.reserve(noComments.size());
		inString = false;
		escape = false;
		for (size_t i = 0; i < noComments.size(); ++i)
		{
			const char c = noComments[i];
			if (inString)
			{
				out.push_back(c);
				if (escape)
				{
					escape = false;
					continue;
				}
				if (c == '\\')
				{
					escape = true;
					continue;
				}
				if (c == '"')
					inString = false;
				continue;
			}
			if (c == '"')
			{
				inString = true;
				out.push_back(c);
				continue;
			}

			if (c == ',')
			{
				// look ahead for next non-whitespace
				size_t j = i + 1;
				while (j < noComments.size() && (noComments[j] == ' ' || noComments[j] == '\t' || noComments[j] == '\r' || noComments[j] == '\n'))
					++j;
				if (j < noComments.size() && (noComments[j] == ']' || noComments[j] == '}'))
					continue;
			}

			out.push_back(c);
		}
		return out;
	}

	struct JsonValue
	{
		enum class Type
		{
			Null,
			Bool,
			String,
			Array,
			Object,
		};
		Type type = Type::Null;
		bool b = false;
		std::string s;
		std::vector<JsonValue> a;
		std::vector<std::pair<std::string, JsonValue>> o;

		const JsonValue* Find(const char* key) const
		{
			if (type != Type::Object)
				return nullptr;
			for (const auto& kv : o)
			{
				if (_stricmp(kv.first.c_str(), key) == 0)
					return &kv.second;
			}
			return nullptr;
		}
	};

	struct JsonParser
	{
		const char* p = nullptr;
		const char* end = nullptr;
		std::string error;

		explicit JsonParser(const std::string& text)
			: p(text.c_str())
			, end(text.c_str() + text.size())
		{
		}

		void SkipWs()
		{
			while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n'))
				++p;
		}

		bool Match(char c)
		{
			SkipWs();
			if (p < end && *p == c)
			{
				++p;
				return true;
			}
			return false;
		}

		bool Expect(char c, const char* what)
		{
			if (Match(c))
				return true;
			error = what;
			return false;
		}

		bool ParseString(std::string& out)
		{
			SkipWs();
			if (p >= end || *p != '"')
			{
				error = "Expected string";
				return false;
			}
			++p;
			std::string s;
			while (p < end)
			{
				char c = *p++;
				if (c == '"')
				{
					out = std::move(s);
					return true;
				}
				if (c == '\\')
				{
					if (p >= end)
					{
						error = "Unterminated escape";
						return false;
					}
					char e = *p++;
					switch (e)
					{
						case '"': s.push_back('"'); break;
						case '\\': s.push_back('\\'); break;
						case '/': s.push_back('/'); break;
						case 'b': s.push_back('\b'); break;
						case 'f': s.push_back('\f'); break;
						case 'n': s.push_back('\n'); break;
						case 'r': s.push_back('\r'); break;
						case 't': s.push_back('\t'); break;
						case 'u':
						{
							unsigned v = 0;
							for (int i = 0; i < 4; ++i)
							{
								if (p >= end)
								{
									error = "Bad unicode escape";
									return false;
								}
								char h = *p++;
								v <<= 4;
								if (h >= '0' && h <= '9') v |= (unsigned)(h - '0');
								else if (h >= 'a' && h <= 'f') v |= (unsigned)(10 + h - 'a');
								else if (h >= 'A' && h <= 'F') v |= (unsigned)(10 + h - 'A');
								else
								{
									error = "Bad unicode hex";
									return false;
								}
							}
							if (v <= 0x7F)
								s.push_back((char)v);
							else
								s.push_back('?');
							break;
						}
						default:
							error = "Unknown escape";
							return false;
					}
					continue;
				}
				s.push_back(c);
			}
			error = "Unterminated string";
			return false;
		}

		bool ParseValue(JsonValue& out)
		{
			SkipWs();
			if (p >= end)
			{
				error = "Unexpected end";
				return false;
			}
			if (*p == '"')
			{
				out.type = JsonValue::Type::String;
				return ParseString(out.s);
			}
			if (*p == '{')
				return ParseObject(out);
			if (*p == '[')
				return ParseArray(out);
			if (end - p >= 4 && std::strncmp(p, "true", 4) == 0)
			{
				p += 4;
				out.type = JsonValue::Type::Bool;
				out.b = true;
				return true;
			}
			if (end - p >= 5 && std::strncmp(p, "false", 5) == 0)
			{
				p += 5;
				out.type = JsonValue::Type::Bool;
				out.b = false;
				return true;
			}
			if (end - p >= 4 && std::strncmp(p, "null", 4) == 0)
			{
				p += 4;
				out.type = JsonValue::Type::Null;
				return true;
			}
			error = "Unexpected token";
			return false;
		}

		bool ParseArray(JsonValue& out)
		{
			out = JsonValue{};
			out.type = JsonValue::Type::Array;
			if (!Expect('[', "Expected '['"))
				return false;
			SkipWs();
			if (Match(']'))
				return true;
			while (p < end)
			{
				JsonValue v;
				if (!ParseValue(v))
					return false;
				out.a.push_back(std::move(v));
				SkipWs();
				if (Match(']'))
					return true;
				if (!Expect(',', "Expected ','"))
					return false;
			}
			error = "Unterminated array";
			return false;
		}

		bool ParseObject(JsonValue& out)
		{
			out = JsonValue{};
			out.type = JsonValue::Type::Object;
			if (!Expect('{', "Expected '{'"))
				return false;
			SkipWs();
			if (Match('}'))
				return true;
			while (p < end)
			{
				std::string key;
				if (!ParseString(key))
					return false;
				if (!Expect(':', "Expected ':'"))
					return false;
				JsonValue v;
				if (!ParseValue(v))
					return false;
				out.o.emplace_back(std::move(key), std::move(v));
				SkipWs();
				if (Match('}'))
					return true;
				if (!Expect(',', "Expected ','"))
					return false;
			}
			error = "Unterminated object";
			return false;
		}
	};
}
