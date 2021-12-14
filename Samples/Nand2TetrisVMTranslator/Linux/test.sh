clear
rm VMTranslate.o > /dev/null 2>&1
make
echo
echo
./VMTranslate.o ../VMSamples/FibonacciElement
