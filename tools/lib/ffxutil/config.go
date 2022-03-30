// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package ffxutil

import (
	"fmt"
	"os"
	"path/filepath"

	"github.com/xeipuuv/gojsonpointer"
	"go.fuchsia.dev/fuchsia/tools/lib/jsonutil"
)

// FFXConfig describes a config to run ffx with.
type FFXConfig struct {
	socket string
	config map[string]interface{}
	env    []string
}

// newIsolatedFFXConfig creates a config that provides an isolated environment to run ffx in.
func newIsolatedFFXConfig(dir string) *FFXConfig {
	socketPath := filepath.Join(dir, "ffx_socket")
	config := &FFXConfig{
		socket: socketPath,
		config: make(map[string]interface{}),
		env:    buildConfigEnv(socketPath),
	}
	config.Set("overnet", map[string]string{"socket": socketPath})
	config.Set(
		"log",
		map[string]interface{}{"dir": []string{filepath.Join(dir, "logs")}},
	)
	config.Set("test", map[string][]string{"output_path": {filepath.Join(dir, "saved_test_runs")}})
	config.Set("fastboot", map[string]map[string]bool{"usb": {"disabled": true}})
	config.Set("ffx", map[string]map[string]bool{"analytics": {"disabled": true}})
	return config
}

func buildConfigEnv(socketPath string) []string {
	// TODO(fxbug.dev/64499): Stop setting environment variables once bug is fixed.
	// The env vars to set come from //src/developer/ffx/plugins/self-test/src/test/mod.rs.
	// TEMP, TMP, HOME, and XDG_CONFIG_HOME are set in //tools/lib/environment/environment.go
	// with a call to environment.Ensure().
	envMap := map[string]string{
		"ASCENDD": socketPath,
	}
	var env []string
	for k, v := range envMap {
		env = append(env, fmt.Sprintf("%s=%s", k, v))
	}
	return env
}

// Env returns the environment that should be set when running ffx with this config.
func (c *FFXConfig) Env() []string {
	return c.env
}

// Set sets values in the config.
func (c *FFXConfig) Set(key string, value interface{}) {
	c.config[key] = value
}

// SetJsonPointer sets the config value at the RFC 6901 JSON Pointer provided.
func (c *FFXConfig) SetJsonPointer(pointer string, value interface{}) error {
	p, err := gojsonpointer.NewJsonPointer(pointer)
	if err != nil {
		return err
	}
	if _, err := p.Set(c.config, value); err != nil {
		return err
	}
	return nil
}

// ToFile writes the config to a file.
func (c *FFXConfig) ToFile(configPath string) error {
	if err := os.MkdirAll(filepath.Dir(configPath), os.ModePerm); err != nil {
		return err
	}
	return jsonutil.WriteToFile(configPath, c.config)
}

func configFromFile(configPath string) (*FFXConfig, error) {
	var config map[string]interface{}
	if err := jsonutil.ReadFromFile(configPath, &config); err != nil {
		return nil, fmt.Errorf("jsonutil.ReadFromFile(%q, _) = %w", configPath, err)
	}
	p, err := gojsonpointer.NewJsonPointer("/overnet/socket")
	if err != nil {
		return nil, err
	}
	socketPathVal, _, err := p.Get(config)
	if err != nil {
		return nil, fmt.Errorf("error getting overnet.socket from config %q: %w", configPath, err)
	}
	socketPath, ok := socketPathVal.(string)
	if !ok {
		return nil, fmt.Errorf("overnet.socket at config %q is not a string", configPath)
	}
	return &FFXConfig{
		socket: socketPath,
		config: config,
		env:    buildConfigEnv(socketPath),
	}, nil
}

// Close removes the socket if it hasn't been removed by `ffx daemon stop`.
func (c *FFXConfig) Close() error {
	if c.socket == "" {
		return nil
	}

	if _, err := os.Stat(c.socket); !os.IsNotExist(err) {
		return os.Remove(c.socket)
	}
	return nil
}
