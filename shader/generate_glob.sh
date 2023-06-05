#!/bin/bash

genfile="shader_glob.asm"

function write_genfile
{
	echo -e -n "$1" >> $genfile
}

function declare_shader_t
{
	echo -e -n "
	struc shadersrc_t
	  .name resq 1
		.addr resq 1
		.len resd 1
	endstruc"
}

function include_shader
{
	local identifier="$(echo $1 | tr '.' '_')"
	identifier=${identifier:0:-4}
	
	echo -e -n "
	align 4
	"$identifier"_addr:
	incbin \"$1\"
	"$identifier"_len equ \$-"$identifier"_addr
	"$identifier"_name db \"$identifier\", 0"
}

function add_shader
{
  local identifier="$(echo $1 | tr '.' '_')"
  identifier=${identifier:0:-4}
	
	echo -e -n "
	global $identifier
	$identifier: istruc shadersrc_t
	  at shadersrc_t.name, dq "$identifier"_name
		at shadersrc_t.addr, dq "$identifier"_addr
		at shadersrc_t.len, dd "$identifier"_len
	iend"
}

if [[ -f $genfile ]]
then
  rm $genfile
  touch $genfile
fi

write_genfile "section .bss\n"
write_genfile "$(declare_shader_t)\n\n"
write_genfile "section .data\n"

for file in "$@"
do
	write_genfile "$(include_shader $file)\n"
done

for file in "$@"
do
	write_genfile "$(add_shader $file)\n"
done