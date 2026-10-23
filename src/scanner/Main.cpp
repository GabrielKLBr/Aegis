#include "Scanner.h"
#ifdef _WIN32
int wmain(int argc, wchar_t **argv)
{
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i)
        args.push_back(av::scan::PathText(std::filesystem::path(argv[i])));
    return av::scan::CliMain(args);
}
#else
int main(int argc, char **argv)
{
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i)
        args.emplace_back(argv[i]);
    return av::scan::CliMain(args);
}
#endif
