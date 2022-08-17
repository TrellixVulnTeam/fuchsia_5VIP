// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Note: The names of the structs in this package match the corresponding clang-doc class names.
package clangdoc

import (
	"log"
	"os"
	"strings"

	"gopkg.in/yaml.v2"
)

type Location struct {
	LineNumber int    `yaml:"LineNumber"`
	Filename   string `yaml:"Filename"`
}

type Type struct {
	Name string `yaml:"Name"`

	// Namespace info. This uses slash separators?!?!
	Path string `yaml:"Path"`
}

// PathNameToFullyQualified converts a Path + Name that is used for types and references into a
// fully-qualified C++ name.
func PathNameToFullyQualified(path, name string) string {
	// The Path seems to use slash separators?!?!?
	if len(path) == 0 {
		// No scoping.
		return name
	}

	// Replace "std::__2" prefixes which are standard library versioning stuff that the user
	// doesn't want to see.
	if strings.HasPrefix(path, "std/__2") {
		path = strings.Replace(path, "std/__2", "std", 1)
	}

	return strings.ReplaceAll(path, "/", "::") + "::" + name
}

// QualifiedName returns a fully-qualified name for the type that takes into account the Path.
func (t Type) QualifiedName() string {
	return PathNameToFullyQualified(t.Path, t.Name)
}

type CommentInfo struct {
	Kind        string        `yaml:"Kind"`
	Text        string        `yaml:"Text"`
	Name        string        `yaml:"Name"`
	Direction   string        `yaml:"Direction"` // in/out for parameter comments
	ParamName   string        `yaml:"ParamName"`
	CloseName   string        `yaml:"CloseName"`
	SelfClosing bool          `yaml:"SelfClosing"`
	Explicit    bool          `yaml:"Explicit"`
	Args        string        `yaml:"Args"`
	AttrKeys    []string      `yaml:"AttrKeys"`
	AttrValues  []string      `yaml:"AttrValues"`
	Children    []CommentInfo `yaml:"Children"`
}

// FieldTypeInfo is a field with a name and a type. It is used for function parameters. See also
// MemberTypeInfo which adds an access tag (basically inheritance).
type FieldTypeInfo struct {
	Name string `yaml:"Name"`
	Type Type   `yaml:"Type"`
}

type MemberTypeInfo struct {
	Name   string `yaml:"Name"`
	Type   Type   `yaml:"Type"`
	Access string `yaml:"Access"`

	// This being present depends on Brett's unlanded clang-doc change.
	Description []CommentInfo `yaml:"Description"`
}

func (m MemberTypeInfo) IsPublic() bool {
	return m.Access == "Public"
}
func (m MemberTypeInfo) IsPrivate() bool {
	// Clang-doc seems to omit access for private members.
	return len(m.Access) == 0 || m.Access == "Private"
}
func (m MemberTypeInfo) IsProtected() bool {
	return m.Access == "Protected"
}

// ReturnType is a struct which clang-doc doesn't have but it inserts another layer of indirection
// in the YAML file which is modeled with this struct.
type ReturnType struct {
	Type Type `yaml:"Type"`
}

type FunctionInfo struct {
	USR  string `yaml:"USR"`
	Name string `yaml:"Name"`

	// See RecordInfo.Namespace for documentation.
	Namespace   []Reference     `yaml:"Namespace"`
	DefLocation Location        `yaml:"DefLocation"` // Definition location
	Description []CommentInfo   `yaml:"Description"`
	Location    []Location      `yaml:"Location"` // Declaration locations.
	Params      []FieldTypeInfo `yaml:"Params"`
	ReturnType  ReturnType      `yaml:"ReturnType"`
	IsMethod    bool            `yaml:"IsMethod"`
	Access      string          `yaml:"Access"` // Public, Private, Protected.
}

func (f FunctionInfo) IsPublic() bool {
	return f.Access == "Public"
}
func (f FunctionInfo) IsPrivate() bool {
	// Clang-doc seems to omit access for private members.
	return len(f.Access) == 0 || f.Access == "Private"
}
func (f FunctionInfo) IsProtected() bool {
	return f.Access == "Protected"
}

// IdentityKey returns a string which represents the function identity, such that string comparisons
// between functions on their identity keys is sufficient for determining whether two functions are
// the same.
//
// This uses the name, return type, and parameter types. It does not include parameter names nor
// member information (this is used to see if a a function has been covered by a base class so
// we explicitly don't want the enclosing class information).
func (f FunctionInfo) IdentityKey() string {
	var result string
	if len(f.ReturnType.Type.Path) > 0 {
		result += f.ReturnType.Type.Path + "::"
	}
	result += f.ReturnType.Type.Name + " "

	result += f.Name + "("

	for i, param := range f.Params {
		if i >= 1 {
			result += ", "
		}
		if len(param.Type.Path) > 0 {
			result += param.Type.Path + "::"
		}
		result += param.Type.Name
	}

	result += ")"
	return result
}

type Reference struct {
	Type                string `yaml:"Type"`
	Name                string `yaml:"Name"`
	USR                 string `yaml:"USR"`
	Path                string `yaml:"Path"`
	IsInGlobalNamespace bool   `yaml:"IsInGlobalNamespace"`
}

type EnumInfo struct {
	Scoped  bool     `yaml:"Scoped"` // True for an enum class.
	Members []string `yaml:"Members"`
}

type RecordInfo struct {
	Name string `yaml:"Name"`
	USR  string `yaml:"USR"`
	Path string `yaml:"Path"`

	// This will be an array of the path to the struct definition. It will include child
	// structs and not just namespaces.
	//
	// The order is going from the namespace closest to the record and moving outward, so to
	// reconstruct the C++ name you would iterate in reverse.
	//
	//   [ { Type: "Record",    Name: "Outer" },
	//     { Type: "Namespace", Name: "ns" } ]
	//
	// Clang-doc generates "GlobalNamespace"-named namespaces for records in the global
	// namespace. This seems incorrect but was added for the way some other backends work:
	//   https://reviews.llvm.org/D66298
	//
	// If using this to generate a name, "GlobalNamespace" will need to be removed manually.
	Namespace []Reference `yaml:"Namespace"`

	DefLocation Location      `yaml:"DefLocation"`
	Description []CommentInfo `yaml:"Description"`

	TagType string           `yaml:"TagType"` // TODO(brettw) missing in current clang-doc output.
	Members []MemberTypeInfo `yaml:"Members"`

	// |Bases| recursively lists all base classes as a way to list the inherited functions.
	// See also |Parents| and |VirtualParents|.
	Bases []*RecordInfo `yaml:"Bases"`

	// The classes that this class derives from directly. Contrast to "Bases" which lists all
	// base classes recursively.
	Parents        []Reference `yaml:"Parents"`
	VirtualParents []Reference `yaml:"VirtualParents"`

	ChildRecordRefs []Reference `yaml:"ChildRecords"`
	ChildRecords    []*RecordInfo

	ChildFunctions []*FunctionInfo `yaml:"ChildFunctions"`
	ChildEnums     []*EnumInfo     `yaml:"ChildEnums"`

	// When this record is a base class, these items hold the derived information.
	Access    string `yaml:"Access"`
	IsVirtual bool   `yaml:"IsVirtual"`

	// I'm not sure what this means, I suspect this is set when the class also appears in the
	// Parents or VirtualParents list.
	IsParent bool `yaml:"IsParent"`
}

func (r RecordInfo) IsConstructor(f *FunctionInfo) bool {
	return r.Name == f.Name
}
func (r RecordInfo) IsDestructor(f *FunctionInfo) bool {
	return "~"+r.Name == f.Name
}

type NamespaceInfo struct {
	Name string `yaml:"Name"`
	USR  string `yaml:"USR"`

	// "Refs" versions expanded by LoadNamespace().
	ChildNamespaceRefs []Reference `yaml:"ChildNamespaces"`
	ChildNamespaces    []*NamespaceInfo
	ChildRecordRefs    []Reference `yaml:"ChildRecords"`
	ChildRecords       []*RecordInfo

	ChildFunctions []*FunctionInfo `yaml:"ChildFunctions"`
	ChildEnums     []*EnumInfo     `yaml:"EnumInfo"`
}

func LoadRecord(dir string, rec Reference) *RecordInfo {
	// Record (structs, classes, etc.) are stored in a file in the same directory
	// with the name of the record. Anonymous records are named by the USR id.
	var recname string
	var memberdir string // Directory where child references go.
	if len(rec.Name) == 0 {
		recname = "@nonymous_record_" + rec.USR
		memberdir = dir
	} else {
		recname = rec.Name
		memberdir = dir + "/" + rec.Name
	}

	filename := dir + "/" + recname + ".yaml"
	content, err := os.ReadFile(filename)
	if err != nil {
		log.Fatal(err)
	}

	r := &RecordInfo{}
	err = yaml.Unmarshal(content, &r)
	if err != nil {
		log.Fatalf("error: %v in file %v", err, filename)
	}

	// Child structs are in a subdirectory with the current struct name.
	for _, c := range r.ChildRecordRefs {
		r.ChildRecords = append(r.ChildRecords, LoadRecord(memberdir, c))
	}
	return r
}

// LoadNamespace loads everything in the given namespace.
//
// |dir| is the name of the namespace to load, and |child_ns_dir| is the directory that contains the
// child namespaces of this one.
//
// The global namespace stores its items in a "GlobalNamespace" directory buts its children are
// siblings of it, while all other namespaces store their children nested inside.
func LoadNamespace(dir string, child_ns_dir string) *NamespaceInfo {
	filename := dir + "/index.yaml"
	content, err := os.ReadFile(filename)
	if err != nil {
		log.Fatal(err)
	}

	ns := &NamespaceInfo{}
	err = yaml.Unmarshal(content, ns)
	if err != nil {
		log.Fatalf("error: %v in file %v", err, filename)
	}

	for _, c := range ns.ChildNamespaceRefs {
		child_dir := child_ns_dir + "/" + c.Name
		ns.ChildNamespaces = append(ns.ChildNamespaces, LoadNamespace(child_dir, child_dir))
	}

	for _, c := range ns.ChildRecordRefs {
		ns.ChildRecords = append(ns.ChildRecords, LoadRecord(dir, c))
	}

	return ns
}

// Returns the root namespace. All other namespaces will be inside of this.
func Load(dir string) *NamespaceInfo {
	return LoadNamespace(dir+"/GlobalNamespace", dir)
}
