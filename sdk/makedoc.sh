grep 'function .* {' stdlib.evm | sed 's/function \(.*\)(\(.*\)) { \/\/ \(.*\)/\1(\2) - \3/g'
