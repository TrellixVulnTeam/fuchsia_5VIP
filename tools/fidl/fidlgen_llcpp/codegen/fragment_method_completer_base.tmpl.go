// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const fragmentMethodCompleterBaseTmpl = `

{{- define "Method:CompleterBase:Header" }}
{{ EnsureNamespace "" }}
{{- if .HasResponse }}
  template<>
  class {{ .WireCompleterBase }} : public ::fidl::CompleterBase {
    public:
    // In the following methods, the return value indicates internal errors during
    // the reply, such as encoding or writing to the transport.
    // Note that any error will automatically lead to the destruction of the binding,
    // after which the |on_unbound| callback will be triggered with a detailed reason.
    //
    // See //zircon/system/ulib/fidl/include/lib/fidl/llcpp/server.h.
    //
    // Because the reply status is identical to the unbinding status, it can be safely ignored.
    ::fidl::Result Reply({{ RenderParams .ResponseArgs }});
        {{- if .Result }}
    ::fidl::Result ReplySuccess({{ RenderParams .Result.ValueMembers }});
    ::fidl::Result ReplyError({{ .Result.ErrorDecl }} error);
        {{- end }}
        {{- if .ResponseArgs }}
    ::fidl::Result Reply({{ RenderParams "::fidl::BufferSpan _buffer" .ResponseArgs }});
          {{- if .Result }}
    ::fidl::Result ReplySuccess(
        {{- RenderParams "::fidl::BufferSpan _buffer" .Result.ValueMembers }});
          {{- end }}
        {{- end }}

    protected:
      using ::fidl::CompleterBase::CompleterBase;
  };

  template<>
  struct {{ .WireMethodTypes }} {
    using Completer = fidl::Completer<{{ .WireCompleterBase }}>;
  };
{{- end }}
{{- end }}

{{- define "Method:CompleterBase:Source" }}
{{ EnsureNamespace "" }}
{{- IfdefFuchsia -}}
::fidl::Result
{{ .WireCompleterBase.NoLeading }}::Reply({{ RenderParams .ResponseArgs }}) {
  FIDL_INTERNAL_DISABLE_AUTO_VAR_INIT
  ::fidl::OwnedEncodedMessage<{{ .WireResponse }}> _response{
    {{- RenderForwardParams "::fidl::internal::AllowUnownedInputRef{}" .ResponseArgs -}}
  };
  return {{ .WireCompleterBase }}::SendReply(&_response.GetOutgoingMessage());
}

{{- if .Result }}
  ::fidl::Result
  {{ .WireCompleterBase.NoLeading }}::ReplySuccess(
        {{ RenderParams .Result.ValueMembers }}) {
    {{ .Result.ValueStructDecl }} _response;
    {{- range .Result.ValueMembers }}
      _response.{{ .Name }} = std::move({{ .Name }});
    {{- end }}

    {{- if .Result.Value.InlineInEnvelope }}
    return Reply({{ .Result.ResultDecl }}::WithResponse(std::move(_response)));
    {{- else }}
    return Reply({{ .Result.ResultDecl }}::WithResponse(
        ::fidl::ObjectView<{{ .Result.ValueStructDecl }}>::FromExternal(&_response)));
    {{- end }}
  }

  ::fidl::Result
  {{ .WireCompleterBase.NoLeading }}::ReplyError({{ .Result.ErrorDecl }} error) {
    {{- if .Result.Error.InlineInEnvelope }}
    return Reply({{ .Result.ResultDecl }}::WithErr(std::move(error)));
    {{- else }}
    return Reply({{ .Result.ResultDecl }}::WithErr(
        ::fidl::ObjectView<{{ .Result.ErrorDecl }}>::FromExternal(&error)));
    {{- end }}
  }
{{- end }}
{{- if .ResponseArgs }}

  ::fidl::Result {{ .WireCompleterBase.NoLeading }}::Reply(
        {{- RenderParams "::fidl::BufferSpan _buffer" .ResponseArgs }}) {
    {{ .WireResponse }}::UnownedEncodedMessage _response(
          {{ RenderForwardParams "_buffer.data" "_buffer.capacity" .ResponseArgs }});
    return CompleterBase::SendReply(&_response.GetOutgoingMessage());
  }

  {{- if .Result }}
    ::fidl::Result {{ .WireCompleterBase.NoLeading }}::ReplySuccess(
          {{- RenderParams "::fidl::BufferSpan _buffer" .Result.ValueMembers }}) {
      {{ .Result.ValueStructDecl }} response;
      {{- range .Result.ValueMembers }}
        response.{{ .Name }} = std::move({{ .Name }});
      {{- end }}
      {{- if .Result.Value.InlineInEnvelope }}
      return Reply(std::move(_buffer), {{ .Result.ResultDecl }}::WithResponse(
            std::move(response)));
      {{- else }}
      return Reply(std::move(_buffer), {{ .Result.ResultDecl }}::WithResponse(
            ::fidl::ObjectView<{{ .Result.ValueStructDecl }}>::FromExternal(&response)));
      {{- end }}
    }
  {{- end }}
{{- end }}
{{- EndifFuchsia -}}
{{- end }}
`
