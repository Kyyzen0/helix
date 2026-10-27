#include "Path.h"
#include <cctype>
#include <string_view>
#include <utility>

helix::Path::Path() : path_("") {}

helix::Path::Path(const std::string& path)
{
    // Trim surrounding whitespace before validating so checks like isRoot are not
    // thrown off by padding such as " / "
    std::string stripped = strip(path);

    if (!isValidFormatting(stripped))
    {
        path_ = "";
        return;
    }

    path_ = std::move(stripped);
}

helix::Path::Path(const char* path) : Path(std::string(path)) {}

std::string helix::Path::strip(const std::string& str)
{
    size_t startWhitespace = str.find_first_not_of(" \t\n\r\f\v");

    if (startWhitespace == std::string::npos) return "";

    size_t endWhitespace = str.find_last_not_of(" \t\n\r\f\v");

    return str.substr(startWhitespace, endWhitespace - startWhitespace + 1);
}

bool helix::Path::isEmpty() const { return path_.empty(); }

bool helix::Path::isRoot() const { return path_ == "/"; }

bool helix::Path::isAbsolute() const { return !isEmpty() && path_.front() == '/'; }

bool helix::Path::isRelative() const { return !isEmpty() && path_.front() != '/'; }

bool helix::Path::isValidName(const std::string& name)
{
    if (name.empty()) return false;

    for (const char character : name)
    {
        // Fails non-alphanumeric characters except underscores
        if (!std::isalnum(static_cast<unsigned char>(character)) && character != '_') return false;
    }

    return true;
}

bool helix::Path::isValidComponent(const std::string& component)
{
    return component == ".." || isValidName(component);
}

bool helix::Path::isValidFormatting(const std::string& pathString)
{
    if (pathString.empty()) return false;

    if (pathString == "/") return true;

    // Create string_view to avoid modifying original string
    std::string_view pathView(pathString);

    // Strip root delimeter
    if (pathView.front() == '/') pathView.remove_prefix(1);

    // Walk the components between the slashes and validate each one
    // e.g. "foo/bar/baz" checks "foo" then "bar" then "baz"
    std::string_view remaining = pathView;
    while (!remaining.empty())
    {
        size_t slashPos = remaining.find('/');
        std::string_view pathComponent = remaining.substr(0, slashPos);

        if (!isValidComponent(std::string(pathComponent))) return false;

        if (slashPos == std::string_view::npos) break;

        remaining = remaining.substr(slashPos + 1);
    }

    return true;
}

bool helix::Path::isValid() const { return isValidFormatting(path_); }

std::string helix::Path::getName() const
{
    if (isEmpty() || isRoot()) return "";

    size_t slashPos = path_.rfind('/');

    if (slashPos == std::string::npos) return path_;

    return path_.substr(slashPos + 1);
}

helix::Path helix::Path::getParent() const
{
    if (isEmpty() || isRoot()) return *this;

    size_t slashPos = path_.rfind('/');

    if (slashPos == std::string::npos) return Path();

    if (slashPos == 0) return Path("/");

    return Path(path_.substr(0, slashPos));
}

std::vector<std::string> helix::Path::split() const
{
    std::vector<std::string> components;

    if (isEmpty() || isRoot()) return components;

    std::string_view view(path_);

    if (view.front() == '/') view.remove_prefix(1);

    while (!view.empty())
    {
        size_t slashPos = view.find('/');
        components.emplace_back(view.substr(0, slashPos));

        if (slashPos == std::string::npos) break;

        view.remove_prefix(slashPos + 1);
    }

    return components;
}

std::vector<helix::Path> helix::Path::getPrefixes() const
{
    std::vector<helix::Path> prefixes;

    if (isEmpty() || isRoot()) return prefixes;

    std::vector<std::string> components = split();

    std::string currentPrefix;
    for (size_t i = 0; i + 1 < components.size(); i++)
    {
        // Delimiter is "/" except before the first component of a relative path
        std::string delimiter = (isAbsolute() || i != 0) ? "/" : "";
        currentPrefix += delimiter + components[i];
        prefixes.emplace_back(currentPrefix);
    }

    return prefixes;
}

const std::string& helix::Path::getString() const { return path_; }

helix::Path helix::Path::append(const helix::Path& path) const
{
    if (path.isEmpty()) return *this;

    if (isEmpty()) return path;

    // Drop the appended path's root delimiter so it joins as a relative segment
    std::string appendedPath = path.isAbsolute() ? path.getString().substr(1) : path.getString();

    // The root already ends in the delimiter, so joining adds nothing
    if (isRoot()) return helix::Path("/" + appendedPath);

    return helix::Path(path_ + "/" + appendedPath);
}

helix::Path helix::Path::increment(int increment) const
{
    if (isEmpty() || isRoot()) return *this;

    std::string name = getName();
    helix::Path parent = getParent();

    // Find where the numerical suffix starts
    size_t digitStartPos = name.size();
    while (digitStartPos > 0 && std::isdigit(static_cast<unsigned char>(name[digitStartPos - 1])))
        digitStartPos--;

    std::string base = name.substr(0, digitStartPos);
    std::string suffix = name.substr(digitStartPos);

    int number = suffix.empty() ? 0 : std::stoi(suffix);
    std::string incrementedName = base + std::to_string(number + increment);

    return parent.append(helix::Path(incrementedName));
}

helix::Path helix::Path::makeRelative() const
{
    if (isEmpty() || isRoot() || isRelative()) return *this;

    return helix::Path(getString().substr(1));
}

helix::Path helix::Path::makeAbsolute() const
{
    if (isEmpty() || isRoot() || isAbsolute()) return *this;

    return helix::Path("/" + getString());
}

helix::Path helix::Path::makeRelativeTo(const helix::Path& anchor) const
{
    // Only paths that share the anchor as a prefix can be made relative to it
    if (!(isAbsolute() && anchor.isAbsolute() && hasPrefix(anchor))) return *this;

    // Account for the anchor being the same as the path
    if (path_ == anchor.getString()) return Path();

    std::string anchorStr = anchor.isRoot() ? "/" : anchor.getString() + "/";
    std::string relative = path_.substr(anchorStr.size());

    return helix::Path(relative);
}

helix::Path helix::Path::makeAbsoluteFrom(const helix::Path& anchor) const
{
    // An absolute path already names its location and only needs its steps collapsed
    Path joined = isAbsolute() ? *this : anchor.append(*this);

    if (!joined.isAbsolute()) return Path();

    std::vector<std::string> resolved;
    for (const std::string& component : joined.split())
    {
        if (component != "..")
            resolved.push_back(component);
        else if (!resolved.empty())
            resolved.pop_back();
    }

    std::string absolutePath;
    for (const std::string& component : resolved) absolutePath += "/" + component;

    return absolutePath.empty() ? Path("/") : Path(absolutePath);
}

bool helix::Path::hasPrefix(const Path& prefix) const
{
    if (prefix.isEmpty()) return false;

    if (prefix.isRoot()) return isAbsolute();

    if (path_ == prefix.getString()) return true;

    return path_.compare(0, prefix.getString().size() + 1, prefix.getString() + "/") == 0;
}

helix::Path::operator std::string_view() const { return path_; }

bool helix::Path::operator==(std::string_view other) const { return path_ == other; }

bool helix::Path::operator!=(std::string_view other) const { return path_ != other; }

std::ostream& helix::operator<<(std::ostream& os, const helix::Path& other)
{
    return os << other.getString();
}
