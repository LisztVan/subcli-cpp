#include "subcli/cli_completion.hpp"

namespace subcli {

std::string generateBashCompletion() {
    return R"BASH(_subcli_completion() {
    local cur prev cmd subcmd
    COMPREPLY=()
    cur="${COMP_WORDS[COMP_CWORD]}"
    prev="${COMP_WORDS[COMP_CWORD-1]}"
    cmd="${COMP_WORDS[1]}"
    subcmd="${COMP_WORDS[2]}"

    if [[ $COMP_CWORD -eq 1 ]]; then
        COMPREPLY=( $(compgen -W "init doctor sub config template export check completion" -- "$cur") )
        return 0
    fi

    case "$cmd" in
        sub)
            if [[ $COMP_CWORD -eq 2 ]]; then
                COMPREPLY=( $(compgen -W "add remove list update enable disable edit validate" -- "$cur") )
                return 0
            fi
            COMPREPLY=( $(compgen -W "--id --name --url --group --format-hint --user-agent --timeout --retry --priority --update-interval --tag --tags --header --force --strict-network --json --enable --disable --clear-headers --remove-header" -- "$cur") )
            ;;
        config)
            if [[ $COMP_CWORD -eq 2 ]]; then
                COMPREPLY=( $(compgen -W "list get set remove" -- "$cur") )
                return 0
            fi
            COMPREPLY=( $(compgen -W "tun output_dir template_dir parallelism timeout retry log_level core_paths.mihomo core_paths.sing_box core_paths.xray node_management.dedupe node_management.rename_template node_management.include_regex node_management.exclude_regex node_management.sort_by fetch_max_bytes --json" -- "$cur") )
            ;;
        template)
            if [[ $COMP_CWORD -eq 2 ]]; then
                COMPREPLY=( $(compgen -W "list get set reset validate" -- "$cur") )
                return 0
            fi
            COMPREPLY=( $(compgen -W "mihomo sing-box xray normal tun --json" -- "$cur") )
            ;;
        export)
            if [[ $COMP_CWORD -eq 2 ]]; then
                COMPREPLY=( $(compgen -W "all mihomo sing-box xray" -- "$cur") )
                return 0
            fi
            COMPREPLY=( $(compgen -W "--tun --check --check-timeout --output-dir --sub --tag --strict-network" -- "$cur") )
            ;;
        check)
            COMPREPLY=( $(compgen -W "mihomo sing-box xray --file --output-dir --timeout" -- "$cur") )
            ;;
        completion)
            COMPREPLY=( $(compgen -W "bash" -- "$cur") )
            ;;
    esac
}

complete -F _subcli_completion subcli
)BASH";
}

} // namespace subcli
