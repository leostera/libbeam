# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Leandro Ostera <leandro@ostera.io>
"""Declared build artifacts inspected by native regression tests."""
load("@rules_cc//cc/common:cc_info.bzl", "CcInfo")

def _generation_configurations_impl(_settings, _attr):
    # Compare ordinary Debug/Release outputs even when the consuming test is
    # running under UBSan. Generators must not depend on host sanitizer flags.
    return {
        name: {
            "//command_line_option:compilation_mode": mode,
            "//command_line_option:copt": [],
            "//command_line_option:linkopt": [],
            "//command_line_option:platform_suffix": "",
        }
        for name, mode in [("debug", "dbg"), ("release", "opt")]
    }

generation_configurations = transition(
    implementation = _generation_configurations_impl,
    inputs = [],
    outputs = [
        "//command_line_option:compilation_mode",
        "//command_line_option:copt",
        "//command_line_option:linkopt",
        "//command_line_option:platform_suffix",
    ],
)

def _cross_configuration_artifacts_impl(ctx):
    outputs = []
    for configuration, targets in ctx.split_attr.targets.items():
        for target in targets:
            for source in target[DefaultInfo].files.to_list():
                # Different configurations have identical short paths. Give
                # both artifacts unique runfiles names without changing bytes.
                output = ctx.actions.declare_file("%s/%s/%s/%s" % (
                    ctx.label.name, configuration, target.label.name, source.basename,
                ))
                ctx.actions.symlink(output = output, target_file = source)
                outputs.append(output)
    return [DefaultInfo(files = depset(outputs), runfiles = ctx.runfiles(files = outputs))]

def _source_inventory_impl(ctx):
    output = ctx.actions.declare_file(ctx.label.name + ".txt")
    ctx.actions.write(output, "\n".join(sorted([file.short_path for file in ctx.files.srcs])) + "\n")
    return [DefaultInfo(files = depset([output]), runfiles = ctx.runfiles(files = ctx.files.srcs + [output]))]

source_inventory = rule(
    implementation = _source_inventory_impl,
    attrs = {"srcs": attr.label_list(mandatory = True, allow_files = True)},
)

RuntimeLinkGraph = provider(fields = ["inputs", "nodes"])

def _runtime_link_graph_impl(target, ctx):
    dependencies = []
    for attribute in ["deps", "implementation_deps", "dynamic_deps"]:
        for dependency in getattr(ctx.rule.attr, attribute, []):
            if RuntimeLinkGraph in dependency:
                dependencies.append(dependency[RuntimeLinkGraph])
    direct = target[CcInfo].linking_context.linker_inputs if CcInfo in target else depset()
    node = json.encode({
        "repository": target.label.workspace_name,
        "package": target.label.package,
        "name": target.label.name,
        "kind": ctx.rule.kind,
        "flags": getattr(ctx.rule.attr, "linkopts", []),
        "additional_inputs": [str(t.label) for t in getattr(ctx.rule.attr, "additional_linker_inputs", [])],
    })
    return [RuntimeLinkGraph(
        inputs = depset(transitive = [direct] + [dep.inputs for dep in dependencies]),
        nodes = depset([node], transitive = [dep.nodes for dep in dependencies]),
    )]

runtime_link_graph = aspect(
    implementation = _runtime_link_graph_impl,
    attr_aspects = ["deps", "implementation_deps", "dynamic_deps"],
)

def _runtime_link_inputs_impl(ctx):
    graph = ctx.attr.target[RuntimeLinkGraph]
    inputs = []
    for item in graph.inputs.to_list():
        libraries = []
        for library in item.libraries:
            libraries.append({
                field: getattr(library, field).short_path if getattr(library, field) else None
                for field in ["static_library", "pic_static_library", "dynamic_library", "interface_library"]
            })
        inputs.append({
            "repository": item.owner.workspace_name,
            "package": item.owner.package,
            "name": item.owner.name,
            "flags": list(item.user_link_flags),
            "libraries": libraries,
            "additional_inputs": [file.short_path for file in item.additional_inputs],
        })
    output = ctx.actions.declare_file(ctx.label.name + ".json")
    ctx.actions.write(output, json.encode({
        "inputs": inputs,
        "nodes": [json.decode(node) for node in graph.nodes.to_list()],
    }))
    return [DefaultInfo(files = depset([output]), runfiles = ctx.runfiles(files = [output]))]

runtime_link_inputs = rule(
    implementation = _runtime_link_inputs_impl,
    attrs = {"target": attr.label(mandatory = True, providers = [CcInfo], aspects = [runtime_link_graph])},
)

cross_configuration_artifacts = rule(
    implementation = _cross_configuration_artifacts_impl,
    attrs = {
        "targets": attr.label_list(mandatory = True, cfg = generation_configurations),
        "_allowlist_function_transition": attr.label(
            default = "@bazel_tools//tools/allowlists/function_transition_allowlist",
        ),
    },
)
