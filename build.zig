// gstream, a native Twitch client on gesso.
//   zig build run                          build and run
//   zig build test                         run the tests
//   zig build -Doptimize=ReleaseFast -Dtarget=x86_64-windows-gnu -p zig-out/windows
// -Dclient-id=ID builds with another Twitch application's Client ID than gstream's own.
const std = @import("std");

// gstream's Twitch application. A Client ID is public: every request carries it, and the binary
// holds it; the application's secret, which device code sign-in does not use, is what stays private.
const client_id_default = "w5v6a57dh8xyu7wvqr5mdatbzetgho";
const zon = @import("build.zig.zon");

const flags: []const []const u8 = &.{ "-std=c11", "-ffp-contract=off", "-Wall", "-Wextra", "-Wno-unused-parameter", "-Wno-missing-field-initializers" };

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});
    const gesso = b.dependency("gesso", .{ .target = target, .optimize = optimize, .video = true }).artifact("gesso");
    const client_id = b.option([]const u8, "client-id", "Twitch application Client ID (default: gstream's)") orelse client_id_default;

    const mod = b.createModule(.{ .target = target, .optimize = optimize, .link_libc = true, .strip = optimize != .Debug });
    mod.addCSourceFiles(.{ .root = b.path("src"), .files = &.{ "main.c", "views.c", "live.c", "player.c", "twitch.c" }, .flags = flags });
    mod.addCMacro("GSTREAM_VERSION", "\"" ++ zon.version ++ "\"");
    mod.addCMacro("GSTREAM_CLIENT_ID", b.fmt("\"{s}\"", .{client_id}));
    mod.linkLibrary(gesso);
    const exe = b.addExecutable(.{ .name = "gstream", .root_module = mod });
    if (target.result.os.tag == .windows) exe.subsystem = .windows;
    b.installArtifact(exe);

    const run = b.addRunArtifact(exe);
    run.step.dependOn(b.getInstallStep());
    if (b.args) |args| run.addArgs(args);
    b.step("run", "Build and run gstream").dependOn(&run.step);

    const tests = b.createModule(.{ .target = target, .optimize = optimize, .link_libc = true });
    tests.addCSourceFiles(.{ .root = b.path("tests"), .files = &.{ "main.c", "twitch.c" }, .flags = flags });
    tests.addCSourceFiles(.{ .root = b.path("src"), .files = &.{"twitch.c"}, .flags = flags });
    tests.addIncludePath(b.path("src"));
    tests.linkLibrary(gesso);
    b.step("test", "Run the tests").dependOn(&b.addRunArtifact(b.addExecutable(.{ .name = "gstream-tests", .root_module = tests })).step);
}

