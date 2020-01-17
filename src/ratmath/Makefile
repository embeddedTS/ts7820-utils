ratmath.a: ratmath.lisp
	ecl -norc \
	-eval '(require :asdf)' \
	-eval '(push "./" asdf:*central-registry*)' \
	-eval '(asdf:make-build :ratmath :type :static-library :move-here "./")' \
	-eval '(quit)'

clean:
	-rm -f ratmath.a
