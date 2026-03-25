/**
 * Shared exec helpers for qlever-cli e2e tests.
 *
 * Two modes are supported, controlled by the QLEVER_DIRECT_EXEC environment
 * variable:
 *
 *   QLEVER_DIRECT_EXEC=1  — calls the binary directly via `sh -c`.  Used when
 *                           the test process itself is already running inside a
 *                           linux/amd64 container where `/workspace/qlever-cli`
 *                           is executable (e.g. the `test-runner` compose
 *                           service).
 *
 *   (default)             — wraps every command in `docker run alpine:3` so
 *                           that the linux/amd64 binary can execute on macOS.
 */

import { execSync } from 'child_process';
import * as fs from 'fs';

const IMAGE_NAME = process.env.QLEVER_TEST_IMAGE ?? 'alpine:3';
const DIRECT_EXEC = process.env.QLEVER_DIRECT_EXEC === '1';

/**
 * Returns `execDocker` and `execDockerRaw` helpers that share `containerCwd`
 * and `workspaceDir` from the call-site.
 */
export function createExecHelpers(containerCwd: string, workspaceDir: string) {
    /**
     * Run `cmd` and return stdout.  Throws on non-zero exit (same as
     * execSync).  Appends `&& sync` to flush writes before the container
     * exits.
     */
    function execDocker(cmd: string, inputString?: string): string {
        const escapedCmd = cmd.replace(/'/g, "'\\''");
        if (DIRECT_EXEC) {
            // When the command ends with ' -' the binary will try to open
            // /dev/stdin as a file.  On Alpine with an anonymous pipe (from
            // execSync's `input:` option) that fails with ENXIO because
            // anonymous pipes can't be re-opened by /proc/self/fd/0.
            // Workaround: write the input to a temp file and substitute it.
            let cmdToRun = escapedCmd;
            let tmpFile: string | null = null;
            if (inputString !== undefined && / -(?= |$)/.test(escapedCmd)) {
                tmpFile = `/tmp/qleverinput${Date.now()}${Math.random().toString(36).replace(/\W/g, '')}`;
                fs.writeFileSync(tmpFile, inputString);
                cmdToRun = escapedCmd.replace(/ -(?= |$)/, ' ' + tmpFile);
            }
            try {
                return execSync(`sh -c '${cmdToRun} && sync'`, {
                    encoding: 'utf-8',
                    cwd: containerCwd,
                    input: tmpFile === null ? inputString : undefined,
                    maxBuffer: 10 * 1024 * 1024,
                    timeout: 300000,
                });
            } catch (error: any) {
                if (error.stdout) console.error(`Stdout: ${error.stdout}`);
                if (error.stderr) console.error(`Stderr: ${error.stderr}`);
                throw error;
            } finally {
                if (tmpFile) {
                    try { fs.unlinkSync(tmpFile); } catch {}
                }
            }
        }
        const inputFlag = inputString !== undefined ? '-i' : '';
        const fullCmd = `docker run --init --rm --platform linux/amd64 --user root ${inputFlag} -v "${workspaceDir}":/workspace -w ${containerCwd} --entrypoint="" ${IMAGE_NAME} sh -c '${escapedCmd} && sync'`;
        try {
            return execSync(fullCmd, {
                encoding: 'utf-8',
                cwd: workspaceDir,
                input: inputString,
                maxBuffer: 10 * 1024 * 1024,
                timeout: 300000,
            });
        } catch (error: any) {
            if (error.stdout) console.error(`Stdout: ${error.stdout}`);
            if (error.stderr) console.error(`Stderr: ${error.stderr}`);
            throw error;
        }
    }

    /**
     * Run `cmd` and always return `{ exitCode, stdout, stderr }` — never
     * throws.  Useful for testing expected failures.
     */
    function execDockerRaw(
        cmd: string,
        inputString?: string,
    ): { exitCode: number; stdout: string; stderr: string } {
        const escapedCmd = cmd.replace(/'/g, "'\\''");
        if (DIRECT_EXEC) {
            let cmdToRun = escapedCmd;
            let tmpFile: string | null = null;
            if (inputString !== undefined && / -(?= |$)/.test(escapedCmd)) {
                tmpFile = `/tmp/qleverinput${Date.now()}${Math.random().toString(36).replace(/\W/g, '')}`;
                fs.writeFileSync(tmpFile, inputString);
                cmdToRun = escapedCmd.replace(/ -(?= |$)/, ' ' + tmpFile);
            }
            try {
                const stdout = execSync(`sh -c '${cmdToRun}'`, {
                    encoding: 'utf-8',
                    cwd: containerCwd,
                    input: tmpFile === null ? inputString : undefined,
                    maxBuffer: 10 * 1024 * 1024,
                });
                return { exitCode: 0, stdout, stderr: '' };
            } catch (error: any) {
                return {
                    exitCode: error.status ?? 1,
                    stdout: error.stdout ?? '',
                    stderr: error.stderr ?? '',
                };
            } finally {
                if (tmpFile) {
                    try { fs.unlinkSync(tmpFile); } catch {}
                }
            }
        }
        const shellCmd = `docker run --init --rm --platform linux/amd64 --user root ${inputString !== undefined ? '-i' : ''} -v "${workspaceDir}":/workspace -w ${containerCwd} --entrypoint="" ${IMAGE_NAME} sh -c '${escapedCmd}'`;
        try {
            const stdout = execSync(shellCmd, {
                encoding: 'utf-8',
                cwd: workspaceDir,
                input: inputString,
                maxBuffer: 10 * 1024 * 1024,
            });
            return { exitCode: 0, stdout, stderr: '' };
        } catch (error: any) {
            return {
                exitCode: error.status ?? 1,
                stdout: error.stdout ?? '',
                stderr: error.stderr ?? '',
            };
        }
    }

    return { execDocker, execDockerRaw };
}
