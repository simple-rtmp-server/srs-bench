.PHONY: help default clean bench test

default: bench test

clean:
	rm -f ./objs/srs_bench ./objs/srs_test

.format.txt: *.go rtc/*.go srs/*.go
	gofmt -w .
	echo "done" > .format.txt

bench: ./objs/srs_bench

./objs/srs_bench: .format.txt *.go rtc/*.go srs/*.go Makefile
	go build -mod=vendor -o objs/srs_bench .

test: ./objs/srs_test

./objs/srs_test: .format.txt *.go rtc/*.go srs/*.go Makefile
	go test ./srs -c -o ./objs/srs_test

help:
	@echo "Usage: make [bench|test]"
	@echo "     bench       Make the bench to ./objs/srs_bench"
	@echo "     test        Make the test tool to ./objs/srs_test"
