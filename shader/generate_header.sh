#!/bin/bash

genfile="shader_include.hpp"

function write_genfile
{
	echo -e -n "$1" >> $genfile
}

function declare_shader_t
{
	echo -e -n "
struct shadersrc_t
{
    char* name;
    uint32_t* addr;
    uint32_t len;
};"

}

function extern_shader
{
	local identifier="$(echo $1 | tr '.' '_')"
	echo -e -n "extern shadersrc_t $identifier;"
}

if [[ -f $genfile ]]
then
  rm $genfile
  touch $genfile
fi

file_guard="$(echo ${genfile^^} | tr '.' '_')"

write_genfile "#ifndef $file_guard\n"
write_genfile "#define $file_guard\n"

write_genfile "$(declare_shader_t)\n\n"

for file in "$@"
do
	write_genfile "$(extern_shader $file)\n"
done

write_genfile "\n#endif //$file_guard"
