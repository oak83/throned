package main

import (
	"archive/zip"
	"os"
	"path/filepath"
	"testing"
)

func TestExtractRejectsPathTraversal(t *testing.T) {
	dir := t.TempDir()
	archivePath := filepath.Join(dir, "unsafe.zip")
	file, err := os.Create(archivePath)
	if err != nil {
		t.Fatal(err)
	}
	writer := zip.NewWriter(file)
	entry, err := writer.Create("../outside.txt")
	if err != nil {
		t.Fatal(err)
	}
	if _, err := entry.Write([]byte("unsafe")); err != nil {
		t.Fatal(err)
	}
	if err := writer.Close(); err != nil {
		t.Fatal(err)
	}
	if err := file.Close(); err != nil {
		t.Fatal(err)
	}

	if err := extract(archivePath, filepath.Join(dir, "stage")); err == nil {
		t.Fatal("expected traversal path to be rejected")
	}
	if _, err := os.Stat(filepath.Join(dir, "outside.txt")); !os.IsNotExist(err) {
		t.Fatalf("archive escaped staging directory: %v", err)
	}
}

func TestExtractAndCopyTree(t *testing.T) {
	dir := t.TempDir()
	archivePath := filepath.Join(dir, "update.zip")
	file, err := os.Create(archivePath)
	if err != nil {
		t.Fatal(err)
	}
	writer := zip.NewWriter(file)
	entry, err := writer.Create("Throned/Throned.exe")
	if err != nil {
		t.Fatal(err)
	}
	if _, err := entry.Write([]byte("new binary")); err != nil {
		t.Fatal(err)
	}
	if err := writer.Close(); err != nil {
		t.Fatal(err)
	}
	if err := file.Close(); err != nil {
		t.Fatal(err)
	}

	stage := filepath.Join(dir, "stage")
	if err := extract(archivePath, stage); err != nil {
		t.Fatal(err)
	}
	destination := filepath.Join(dir, "install")
	if err := copyTree(filepath.Join(stage, rootName), destination); err != nil {
		t.Fatal(err)
	}
	data, err := os.ReadFile(filepath.Join(destination, "Throned.exe"))
	if err != nil {
		t.Fatal(err)
	}
	if string(data) != "new binary" {
		t.Fatalf("copied contents = %q", data)
	}
}
