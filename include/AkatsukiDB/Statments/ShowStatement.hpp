#include <optional>
#include <string>

class ShowStatement : public IStatement {
public:
    std::string What;
    std::optional<std::string> Target;
};
