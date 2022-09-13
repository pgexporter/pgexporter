#/usr/bin/env bash

# COMP_WORDS contains
# at index 0 the executable name (pgexporter-cli)
# at index 1 the command name (e.g., backup)
pgexporter_cli_completions()
{

    if [ "${#COMP_WORDS[@]}" == "2" ]; then
        # main completion: the user has specified nothing at all
        # or a single word, that is a command
        COMPREPLY=($(compgen -W "is-alive stop status details reload reset" "${COMP_WORDS[1]}"))
    fi
}


pgexporter_admin_completions()
{
    if [ "${#COMP_WORDS[@]}" == "2" ]; then
        # main completion: the user has specified nothing at all
        # or a single word, that is a command
        COMPREPLY=($(compgen -W "master-key add-user update-user remove-user list-users" "${COMP_WORDS[1]}"))
    fi
}

# install the completion functions
complete -F pgexporter_cli_completions pgexporter-cli
complete -F pgexporter_admin_completions pgexporter-admin
