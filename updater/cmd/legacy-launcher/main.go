package main

import (
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
)

func main() {
	name := "Throned"
	if runtime.GOOS == "windows" {
		name += ".exe"
	}
	path := filepath.Join(filepath.Dir(os.Args[0]), name)
	command := exec.Command(path, os.Args[1:]...)
	command.Dir = filepath.Dir(path)
	_ = command.Start()
}
