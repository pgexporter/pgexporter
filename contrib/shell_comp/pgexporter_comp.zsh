#compdef _pgexporter_cli pgexporter-cli
#compdef _pgexporter_admin pgexporter-admin


function _pgexporter_cli()
{
    local line
    _arguments -C \
               "1: :(is-alive stop status details reload reset)" \
               "*::arg:->args"
}

function _pgexporter_admin()
{
    local line
    _arguments -C \
               "1: :(master-key add-user update-user remove-user list-users)" \
               "*::arg:->args"
}
