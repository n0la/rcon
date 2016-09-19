# rcon bash completion

_rcon()
{
    local cur prev words cword split

    _init_completion || return

    lngopts="--config --help --host --port --password --server"
    shtopts="-c -H -h -p -P -s"
    configfile="$HOME/.rconrc"

    case "${prev}" in
        -c|--config)
            _filedir
            return
            ;;

        -H|--host)
            _known_hosts_real "$cur"
            return
            ;;

        -s|--server)
            servers=$(egrep "^\s*\\[.*?\\]" "${configfile}" 2>/dev/null | tr -d '[]')
            COMPREPLY=( $(compgen -W '"${servers}"' -- "$cur") )
            return
            ;;
    esac

    if [[ $cur == -* ]]; then
        COMPREPLY=( $(compgen -W '$("$1" --help | _parse_help -)' -- "$cur") )
    fi
} &&
complete -F _rcon rcon
