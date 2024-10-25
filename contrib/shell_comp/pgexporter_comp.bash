#/usr/bin/env bash

# COMP_WORDS contains
# at index 0 the executable name (pgexporter-cli)
# at index 1 the command name (e.g., backup)
pgexporter_cli_completions()
{
    if [ "${#COMP_WORDS[@]}" == "2" ]; then
        # main completion: the user has specified nothing at all
        # or a single word, that is a command
        COMPREPLY=($(compgen -W "ping shutdown status conf clear" "${COMP_WORDS[1]}"))
    else
        # the user has specified something else
        # subcommand required?
        case ${COMP_WORDS[1]} in
            status)
                COMPREPLY+=($(compgen -W "details" "${COMP_WORDS[2]}"))
                ;;
            conf)
                COMPREPLY+=($(compgen -W "reload" "${COMP_WORDS[2]}"))
                ;;
            clear)
                COMPREPLY+=($(compgen -W "prometheus" "${COMP_WORDS[2]}"))
                ;;
        esac
    fi
}


pgexporter_admin_completions()
{
    if [ "${#COMP_WORDS[@]}" == "2" ]; then
        # main completion: the user has specified nothing at all
        # or a single word, that is a command
        COMPREPLY=($(compgen -W "master-key user" "${COMP_WORDS[1]}"))
    else
        # the user has specified something else
        # subcommand required?
        case ${COMP_WORDS[1]} in
            user)
                COMPREPLY+=($(compgen -W "add del edit ls" "${COMP_WORDS[2]}"))
                ;;
        esac
    fi
}

# install the completion functions
complete -F pgexporter_cli_completions pgexporter-cli
complete -F pgexporter_admin_completions pgexporter-admin
