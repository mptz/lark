make_library(libutil,
	     base64.c bytebuf.c circlist.c
	     fdutil.c fgh.c fghk.c
	     hashtab.c memutil.c message.c page.c
	     sha2.c symtab.c twister.c twofish.c
	     utf8.c util.c wordbuf.c wordtab.c)
make_binary(hashtest, hashtest.c, util)
make_binary(huidgen, huidgen.c, util)
make_binary(test2fish, test2fish.c, util)
