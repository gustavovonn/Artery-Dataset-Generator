#ifndef DATASET_JSON_H_
#define DATASET_JSON_H_

#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>
#include <type_traits>

namespace dataset
{

// Minimal single-line JSON object builder for JSONL output
class JsonObject
{
public:
    template<typename T, std::enable_if_t<std::is_integral<T>::value && !std::is_same<T, bool>::value, int> = 0>
    JsonObject& field(const char* key, T value)
    {
        return raw(key, std::to_string(value));
    }

    JsonObject& field(const char* key, double value)
    {
        if (!std::isfinite(value)) {
            return null(key);
        }
        std::ostringstream os;
        os << std::setprecision(12) << value;
        return raw(key, os.str());
    }

    JsonObject& field(const char* key, bool value) { return raw(key, value ? "true" : "false"); }
    JsonObject& field(const char* key, const char* value) { return raw(key, quote(value)); }
    JsonObject& field(const char* key, const std::string& value) { return raw(key, quote(value)); }
    JsonObject& field(const char* key, const JsonObject& value) { return raw(key, value.str()); }

    template<typename T>
    JsonObject& optional(const char* key, bool present, T value)
    {
        return present ? field(key, value) : null(key);
    }

    JsonObject& null(const char* key) { return raw(key, "null"); }

    JsonObject& raw(const char* key, const std::string& json)
    {
        if (!mBody.empty()) {
            mBody += ',';
        }
        mBody += quote(key);
        mBody += ':';
        mBody += json;
        return *this;
    }

    std::string str() const { return "{" + mBody + "}"; }

    static std::string quote(const std::string& s)
    {
        std::string out = "\"";
        for (char c : s) {
            switch (c) {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                default: out += c;
            }
        }
        return out + "\"";
    }

private:
    std::string mBody;
};

class JsonArray
{
public:
    template<typename T>
    JsonArray& add(T value)
    {
        JsonObject tmp;
        tmp.field("v", value);
        const std::string s = tmp.str();
        // strip the {"v": ... } wrapper
        return raw(s.substr(5, s.size() - 6));
    }

    JsonArray& add(const JsonObject& value) { return raw(value.str()); }

    JsonArray& raw(const std::string& json)
    {
        if (!mBody.empty()) {
            mBody += ',';
        }
        mBody += json;
        return *this;
    }

    bool empty() const { return mBody.empty(); }
    std::string str() const { return "[" + mBody + "]"; }

private:
    std::string mBody;
};

}  // namespace dataset

#endif /* DATASET_JSON_H_ */
