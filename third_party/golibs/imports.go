// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package imports

import (
	_ "cloud.google.com/go/storage"
	_ "github.com/creack/pty"
	_ "github.com/dustin/go-humanize"
	_ "github.com/dustin/go-humanize/english"
	_ "github.com/flynn/go-tuf"
	_ "github.com/flynn/go-tuf/data"
	_ "github.com/flynn/go-tuf/sign"
	_ "github.com/fsnotify/fsnotify"
	_ "github.com/golang/glog"
	_ "github.com/golang/protobuf/descriptor"
	_ "github.com/golang/protobuf/jsonpb"
	_ "github.com/golang/protobuf/proto"
	_ "github.com/golang/protobuf/protoc-gen-go/descriptor"
	_ "github.com/golang/protobuf/ptypes/empty"
	_ "github.com/google/go-cmp/cmp"
	_ "github.com/google/go-cmp/cmp/cmpopts"
	_ "github.com/google/shlex"
	_ "github.com/google/subcommands"
	_ "github.com/kr/fs"
	_ "github.com/kr/pretty"
	_ "github.com/pkg/sftp"
	_ "github.com/spf13/pflag"
	_ "go.uber.org/multierr"
	_ "golang.org/x/crypto/openpgp"
	_ "golang.org/x/crypto/ssh"
	_ "golang.org/x/net/ipv4"
	_ "golang.org/x/net/ipv6"
	_ "golang.org/x/sync/errgroup"
	_ "golang.org/x/sys/unix"
	_ "gonum.org/v1/gonum/integrate/quad"
	_ "google.golang.org/grpc"
	_ "google.golang.org/grpc/codes"
	_ "google.golang.org/grpc/status"
	_ "google.golang.org/protobuf/encoding/protojson"
	_ "google.golang.org/protobuf/encoding/prototext"
	_ "google.golang.org/protobuf/proto"
	_ "google.golang.org/protobuf/reflect/protoreflect"
	_ "google.golang.org/protobuf/runtime/protoimpl"
	_ "google.golang.org/protobuf/testing/protocmp"
	_ "google.golang.org/protobuf/types/descriptorpb"
	_ "google.golang.org/protobuf/types/known/durationpb"
	_ "google.golang.org/protobuf/types/known/timestamppb"
	_ "gopkg.in/yaml.v2"
	_ "gvisor.dev/gvisor/pkg/log"
	_ "gvisor.dev/gvisor/pkg/tcpip"
	_ "gvisor.dev/gvisor/pkg/tcpip/buffer"
	_ "gvisor.dev/gvisor/pkg/tcpip/faketime"
	_ "gvisor.dev/gvisor/pkg/tcpip/header"
	_ "gvisor.dev/gvisor/pkg/tcpip/link/ethernet"
	_ "gvisor.dev/gvisor/pkg/tcpip/link/loopback"
	_ "gvisor.dev/gvisor/pkg/tcpip/link/nested"
	_ "gvisor.dev/gvisor/pkg/tcpip/link/pipe"
	_ "gvisor.dev/gvisor/pkg/tcpip/link/sniffer"
	_ "gvisor.dev/gvisor/pkg/tcpip/network/arp"
	_ "gvisor.dev/gvisor/pkg/tcpip/network/ipv4"
	_ "gvisor.dev/gvisor/pkg/tcpip/network/ipv6"
	_ "gvisor.dev/gvisor/pkg/tcpip/stack"
	_ "gvisor.dev/gvisor/pkg/tcpip/transport/icmp"
	_ "gvisor.dev/gvisor/pkg/tcpip/transport/packet"
	_ "gvisor.dev/gvisor/pkg/tcpip/transport/tcp"
	_ "gvisor.dev/gvisor/pkg/tcpip/transport/udp"
	_ "gvisor.dev/gvisor/pkg/waiter"
)
