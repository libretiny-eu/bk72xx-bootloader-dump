#!/bin/bash

mkdir -p dist/

envs=("bk7231t" "bk7231n")
for env in ${envs[@]}; do
	prefix="dist/${env}-bootloader-dump_v$1"
	cp .pio/build/${env}/raw_firmware.bin "${prefix}.bin"
	cp .pio/build/${env}/*.ota.rbl "${prefix}.default.rbl"

	ltchiptool soc bkpackager ota "${prefix}.bin" "${prefix}.no-aes.rbl" gzip none --version "$1-${env}"
	ltchiptool soc bkpackager ota "${prefix}.bin" "${prefix}.sber1.rbl" gzip aes256 --version "$1-${env}" --key BDB4CE110F4787C2F539BF50E30A14E0 --iv 46DE464946314329
	ltchiptool soc bkpackager ota "${prefix}.bin" "${prefix}.sber2.rbl" gzip aes256 --version "$1-${env}" --key 5AC0EE0B2379005E3F52CCA49B2D741D --iv 8BCC2EF2EDEC53C7
done
