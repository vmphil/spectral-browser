VERSION := 1.3.3
BIN := bin/spectral-browser
EXT := bin/ext/libspectral-ext.so
WK := webkit2gtk-web-extension-4.1

.PHONY: build ext run fmt vet test clean deb blocklist
build: ext
	go build -o $(BIN) .
ext: $(EXT)
$(EXT): ext/spectral-ext.c
	mkdir -p bin/ext
	gcc -shared -fPIC -O2 -Wall -o $@ $< $$(pkg-config --cflags --libs $(WK))
run: build
	./$(BIN) --debug
fmt:
	gofmt -w .
vet:
	go vet ./...
test:
	go test ./...
clean:
	rm -rf bin
# Refresh the bundled tracker list from EasyPrivacy + EasyList.
blocklist:
	curl -fsSL https://easylist.to/easylist/easyprivacy.txt https://easylist.to/easylist/easylist.txt \
	  | go run ./cmd/genblocklist > blocklist/trackers.txt
deb:
	VERSION=$(VERSION) ./packaging/build-deb.sh
