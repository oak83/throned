package main

import (
	"archive/zip"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
)

const (
	packageName = "Throned.zip"
	stageName   = ".throned-update"
	rootName    = "Throned"
)

func main() {
	if err := update(); err != nil {
		_, _ = fmt.Fprintln(os.Stderr, "Throned update failed:", err)
		os.Exit(1)
	}

	executable := "./Throned"
	if runtime.GOOS == "windows" {
		executable += ".exe"
	}
	if err := exec.Command(executable).Start(); err != nil {
		_, _ = fmt.Fprintln(os.Stderr, "could not restart Throned:", err)
		os.Exit(1)
	}
}

func update() error {
	if _, err := os.Stat(packageName); err != nil {
		return fmt.Errorf("update package not found: %w", err)
	}
	if err := os.RemoveAll(stageName); err != nil {
		return fmt.Errorf("clear staging directory: %w", err)
	}
	defer os.RemoveAll(stageName)

	if err := extract(packageName, stageName); err != nil {
		return err
	}
	source := filepath.Join(stageName, rootName)
	if info, err := os.Stat(source); err != nil || !info.IsDir() {
		return fmt.Errorf("%s directory is missing from update package", rootName)
	}
	if err := copyTree(source, "."); err != nil {
		return err
	}
	if err := os.Remove(packageName); err != nil {
		return fmt.Errorf("remove update package: %w", err)
	}
	return nil
}

func extract(source, destination string) error {
	archive, err := zip.OpenReader(source)
	if err != nil {
		return fmt.Errorf("open update package: %w", err)
	}
	defer archive.Close()

	root, err := filepath.Abs(destination)
	if err != nil {
		return err
	}
	for _, entry := range archive.File {
		clean := filepath.Clean(filepath.FromSlash(entry.Name))
		if clean == "." || filepath.IsAbs(clean) || clean == ".." ||
			strings.HasPrefix(clean, ".."+string(filepath.Separator)) {
			return fmt.Errorf("unsafe path in update package: %q", entry.Name)
		}
		target := filepath.Join(root, clean)
		if target != root && !strings.HasPrefix(target, root+string(filepath.Separator)) {
			return fmt.Errorf("path escapes staging directory: %q", entry.Name)
		}
		if entry.FileInfo().IsDir() {
			if err := os.MkdirAll(target, 0o755); err != nil {
				return err
			}
			continue
		}
		if err := os.MkdirAll(filepath.Dir(target), 0o755); err != nil {
			return err
		}
		input, err := entry.Open()
		if err != nil {
			return err
		}
		mode := entry.Mode()
		if mode == 0 {
			mode = 0o644
		}
		output, err := os.OpenFile(target, os.O_CREATE|os.O_TRUNC|os.O_WRONLY, mode)
		if err != nil {
			input.Close()
			return err
		}
		_, copyErr := io.Copy(output, input)
		closeErr := output.Close()
		input.Close()
		if copyErr != nil {
			return copyErr
		}
		if closeErr != nil {
			return closeErr
		}
	}
	return nil
}

func copyTree(source, destination string) error {
	if err := os.MkdirAll(destination, 0o755); err != nil {
		return err
	}
	return filepath.Walk(source, func(path string, info os.FileInfo, walkErr error) error {
		if walkErr != nil {
			return walkErr
		}
		relative, err := filepath.Rel(source, path)
		if err != nil || relative == "." {
			return err
		}
		target := filepath.Join(destination, relative)
		if info.IsDir() {
			return os.MkdirAll(target, info.Mode())
		}
		input, err := os.Open(path)
		if err != nil {
			return err
		}
		output, err := os.OpenFile(target, os.O_CREATE|os.O_TRUNC|os.O_WRONLY, info.Mode())
		if err != nil {
			input.Close()
			return err
		}
		_, copyErr := io.Copy(output, input)
		closeErr := output.Close()
		input.Close()
		if copyErr != nil {
			return copyErr
		}
		return closeErr
	})
}
