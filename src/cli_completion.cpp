#include "subcli/cli_completion.hpp"
#include "subcli/registry.hpp"

#include <string>
#include <vector>

namespace subcli {

namespace {

std::string joinWords(const std::vector<std::string>& words) {
    std::string out;
    for (size_t i = 0; i < words.size(); ++i) {
        if (i > 0) {
            out += " ";
        }
        out += words[i];
    }
    return out;
}

std::string commandWords() {
    return joinWords(allCommandNames());
}

std::string configWords(bool includeJson) {
    auto keys = allConfigKeyNames();
    if (includeJson) {
        keys.push_back("--json");
    }
    return joinWords(keys);
}

std::string exportWords() {
    auto targets = allExportTargetIds();
    targets.insert(targets.begin(), "all");
    return joinWords(targets);
}

} // namespace

std::string generateBashCompletion() {
    const std::string scriptTemplate = R"BASH(_subcli_completion() {
    local cur prev cmd subcmd
    COMPREPLY=()
    cur="${COMP_WORDS[COMP_CWORD]}"
    prev="${COMP_WORDS[COMP_CWORD-1]}"
    cmd="${COMP_WORDS[1]}"
    subcmd="${COMP_WORDS[2]}"

    if [[ $COMP_CWORD -eq 1 ]]; then
        COMPREPLY=( $(compgen -W "@COMMAND_WORDS@" -- "$cur") )
        return 0
    fi

    case "$cmd" in
        sub)
            if [[ $COMP_CWORD -eq 2 ]]; then
                COMPREPLY=( $(compgen -W "add remove list update enable disable edit validate import export check prune" -- "$cur") )
                return 0
            fi
            COMPREPLY=( $(compgen -W "--id --name --url --group --format-hint --user-agent --timeout --retry --priority --update-interval --tag --tags --header --force --strict-network --json --enable --disable --clear-headers --remove-header" -- "$cur") )
            ;;
        config)
            if [[ $COMP_CWORD -eq 2 ]]; then
                COMPREPLY=( $(compgen -W "list get set remove" -- "$cur") )
                return 0
            fi
            if [[ "$subcmd" == "list" ]]; then
                COMPREPLY=( $(compgen -W "@CONFIG_WORDS_WITH_JSON@" -- "$cur") )
            elif [[ "$subcmd" == "get" || "$subcmd" == "set" || "$subcmd" == "remove" ]]; then
                COMPREPLY=( $(compgen -W "@CONFIG_WORDS@" -- "$cur") )
            else
                COMPREPLY=()
            fi
            ;;
        template)
            if [[ $COMP_CWORD -eq 2 ]]; then
                COMPREPLY=( $(compgen -W "list get set reset validate" -- "$cur") )
                return 0
            fi
            COMPREPLY=( $(compgen -W "mihomo sing-box xray normal tun --json" -- "$cur") )
            ;;
        asset)
            COMPREPLY=( $(compgen -W "list status validate update" -- "$cur") )
            ;;
        profile)
            if [[ $COMP_CWORD -eq 2 ]]; then
                COMPREPLY=( $(compgen -W "list get validate explain" -- "$cur") )
                return 0
            fi
            if [[ "$cmd $subcmd" == "profile explain" ]]; then
                if [[ "$prev" == "--target" ]]; then
                    COMPREPLY=( $(compgen -W "all mihomo sing-box xray" -- "$cur") )
                else
                    COMPREPLY=( $(compgen -W "--target --json" -- "$cur") )
                fi
                return 0
            fi
            COMPREPLY=()
            ;;
        export)
            if [[ $COMP_CWORD -eq 2 ]]; then
                COMPREPLY=( $(compgen -W "@EXPORT_WORDS@" -- "$cur") )
                return 0
            fi
            COMPREPLY=( $(compgen -W "--tun --check --check-timeout --output-dir --profile --sub --tag --strict-network --download-assets --strict-capabilities --explain-policy --json" -- "$cur") )
            ;;
        daemon)
            if [[ $COMP_CWORD -eq 2 ]]; then
                COMPREPLY=( $(compgen -W "once run start stop status" -- "$cur") )
                return 0
            fi
            if [[ "$subcmd" == "stop" || "$subcmd" == "status" ]]; then
                COMPREPLY=()
                return 0
            fi
            COMPREPLY=( $(compgen -W "--interval --target --update-assets --strict-network --check --no-restart" -- "$cur") )
            ;;
        run|restart)
            if [[ $COMP_CWORD -eq 2 ]]; then
                COMPREPLY=( $(compgen -W "mihomo sing-box xray" -- "$cur") )
                return 0
            fi
            COMPREPLY=( $(compgen -W "--file --output-dir" -- "$cur") )
            ;;
        stop|status)
            COMPREPLY=( $(compgen -W "mihomo sing-box xray" -- "$cur") )
            ;;
        check)
            COMPREPLY=( $(compgen -W "mihomo sing-box xray --file --output-dir --timeout" -- "$cur") )
            ;;
        completion)
            COMPREPLY=( $(compgen -W "bash" -- "$cur") )
            ;;
        workspace)
            COMPREPLY=( $(compgen -W "init status use unset migrate doctor" -- "$cur") )
            ;;
    esac
}

complete -F _subcli_completion subcli
)BASH";

    std::string script = scriptTemplate;
    auto replaceAll = [&script](const std::string& from, const std::string& to) {
        size_t pos = 0;
        while ((pos = script.find(from, pos)) != std::string::npos) {
            script.replace(pos, from.size(), to);
            pos += to.size();
        }
    };

    replaceAll("@COMMAND_WORDS@", commandWords());
    replaceAll("@CONFIG_WORDS_WITH_JSON@", configWords(true));
    replaceAll("@CONFIG_WORDS@", configWords(false));
    replaceAll("@EXPORT_WORDS@", exportWords());
    return script;
}

} // namespace subcli
