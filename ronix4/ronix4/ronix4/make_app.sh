for q in $(ls app/*.evm); do sdk/toolchain/evmcomp $q; echo $q; done
