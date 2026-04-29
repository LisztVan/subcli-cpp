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
        COMPREPLY=( $(compgen -W "init doctor sub config template asset profile export daemon run stop status restart check completion workspace" -- "$cur") )
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
            COMPREPLY=( $(compgen -W "tun profile output_dir template_dir asset_dir parallelism timeout retry log_level core_paths.mihomo core_paths.sing_box core_paths.xray node_management.dedupe node_management.rename_template node_management.include_regex node_management.exclude_regex node_management.sort_by fetch_max_bytes --json" -- "$cur") )
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
                COMPREPLY=( $(compgen -W "list get validate" -- "$cur") )
                return 0
            fi
            COMPREPLY=( $(compgen -W "bypass-cn global direct" -- "$cur") )
            ;;
        export)
            if [[ $COMP_CWORD -eq 2 ]]; then
                COMPREPLY=( $(compgen -W "all mihomo sing-box xray" -- "$cur") )
                return 0
            fi
            COMPREPLY=( $(compgen -W "--tun --check --check-timeout --output-dir --profile --sub --tag --strict-network --download-assets" -- "$cur") )
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
}

} // namespace subcli
