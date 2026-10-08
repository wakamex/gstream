// streamit, a native Twitch client on gesso.
//   zig build run                          build and run
//   zig build test                         run the tests
//   zig build -Doptimize=ReleaseFast -Dtarget=x86_64-windows-gnu -p zig-out/windows
// The Twitch application's Client ID comes from -Dclient-id or TWITCH_CLIENT_ID in .env.
const std = @import("std");

const flags: []const []const u8 = &.{ "-std=c11", "-ffp-contract=off", "-Wall", "-Wextra", "-Wno-unused-parameter", "-Wno-missing-field-initializers" };

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});
    const gesso = b.dependency("gesso", .{ .target = target, .optimize = optimize, .video = true }).artifact("gesso");
    const client_id = b.option([]const u8, "client-id", "Twitch application Client ID (default: TWITCH_CLIENT_ID in .env)") orelse envClientId(b);

    const mod = b.createModule(.{ .target = target, .optimize = optimize, .link_libc = true, .strip = optimize != .Debug });
    mod.addCSourceFiles(.{ .root = b.path("src"), .files = &.{ "main.c", "views.c", "live.c", "twitch.c" }, .flags = flags });
    mod.addCMacro("STREAMIT_CLIENT_ID", b.fmt("\"{s}\"", .{client_id}));
    mod.linkLibrary(gesso);
    const exe = b.addExecutable(.{ .name = "streamit", .root_module = mod });
    if (target.result.os.tag == .windows) exe.subsystem = .windows;
    b.installArtifact(exe);

    const run = b.addRunArtifact(exe);
    run.step.dependOn(b.getInstallStep());
    if (b.args) |args| run.addArgs(args);
    b.step("run", "Build and run streamit").dependOn(&run.step);

    const tests = b.createModule(.{ .target = target, .optimize = optimize, .link_libc = true });
    tests.addCSourceFiles(.{ .root = b.path("tests"), .files = &.{ "main.c", "twitch.c" }, .flags = flags });
    tests.addCSourceFiles(.{ .root = b.path("src"), .files = &.{"twitch.c"}, .flags = flags });
    tests.addIncludePath(b.path("src"));
    tests.linkLibrary(gesso);
    b.step("test", "Run the tests").dependOn(&b.addRunArtifact(b.addExecutable(.{ .name = "streamit-tests", .root_module = tests })).step);
}

// The public Client ID from a local .env (TWITCH_CLIENT_ID=...), or empty: sign-in then says it is missing.
fn envClientId(b: *std.Build) []const u8 {
    const env = b.build_root.handle.readFileAlloc(b.graph.io, ".env", b.allocator, .limited(64 * 1024)) catch return "";
    var lines = std.mem.tokenizeAny(u8, env, "\r\n");
    while (lines.next()) |line| {
        const key = "TWITCH_CLIENT_ID=";
        if (std.mem.startsWith(u8, line, key)) return std.mem.trim(u8, line[key.len..], " \t\"'");
    }
    return "";
}
