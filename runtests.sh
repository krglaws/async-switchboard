
ERRTITLE="TESTS FAILED:"
FAILED=""

BUILDDIR="./build"

if [ ! -d $BUILDDIR ] || [ -z "$(ls $BUILDDIR/test_*)" ];
then
    echo "Tests have not been built; run 'make test'"
    exit 1
fi

echo ==============================
echo ----------------------------
echo Running all tests...

for TESTFILE in $(ls $BUILDDIR/test_*)
do
	$TESTFILE
	if [ $? != 0 ]
	then
		FAILED="${FAILED}${TESTFILE}\n"
        echo $TESTFILE failed.
    else
        echo $TESTFILE succeeded.
	fi
done

echo Done.
echo ----------------------------
echo ==============================

if [ "$FAILED" != "" ]
then
	echo $ERRTITLE
	printf $FAILED
	exit 1
fi

exit 0 


