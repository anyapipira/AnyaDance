[CmdletBinding(DefaultParameterSetName = "Listen")]
param(
    [ValidatePattern('^(?:\d{1,3}\.){3}\d{1,3}$')]
    [string]$Group = "239.255.39.71",

    [ValidateRange(1, 65535)]
    [int]$Port = 39571,

    [Parameter(ParameterSetName = "Listen")]
    [string]$Name = "listener-$PID",

    [Parameter(Mandatory, ParameterSetName = "Validate")]
    [switch]$Validate,

    [Parameter(ParameterSetName = "Validate")]
    [ValidateRange(2, 16)]
    [int]$ListenerCount = 2,

    [Parameter(ParameterSetName = "Validate")]
    [ValidateRange(1, 60)]
    [int]$TimeoutSeconds = 8
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$receiver = {
    param(
        [string]$MulticastGroup,
        [int]$MulticastPort,
        [string]$ListenerName,
        [string]$ExpectedToken,
        [int]$ReceiveTimeoutSeconds
    )

    $client = [System.Net.Sockets.UdpClient]::new(
        [System.Net.Sockets.AddressFamily]::InterNetwork)
    try {
        $client.ExclusiveAddressUse = $false
        $client.Client.SetSocketOption(
            [System.Net.Sockets.SocketOptionLevel]::Socket,
            [System.Net.Sockets.SocketOptionName]::ReuseAddress,
            $true)
        $client.Client.Bind(
            [System.Net.IPEndPoint]::new([System.Net.IPAddress]::Any, $MulticastPort))
        $client.JoinMulticastGroup(
            [System.Net.IPAddress]::Parse($MulticastGroup),
            [System.Net.IPAddress]::Loopback)
        $client.MulticastLoopback = $true
        $client.Client.ReceiveTimeout = 250

        if ([string]::IsNullOrEmpty($ExpectedToken)) {
            Write-Host (
                "{0} joined {1}:{2} on loopback. Press Ctrl+C to stop." -f
                $ListenerName,
                $MulticastGroup,
                $MulticastPort)
        }

        $deadline = if ([string]::IsNullOrEmpty($ExpectedToken)) {
            [DateTime]::MaxValue
        } else {
            [DateTime]::UtcNow.AddSeconds($ReceiveTimeoutSeconds)
        }

        while ([DateTime]::UtcNow -lt $deadline) {
            try {
                $remoteEndpoint = [System.Net.IPEndPoint]::new(
                    [System.Net.IPAddress]::Any,
                    0)
                $bytes = $client.Receive([ref]$remoteEndpoint)
            } catch [System.Net.Sockets.SocketException] {
                if ($_.Exception.SocketErrorCode -eq
                    [System.Net.Sockets.SocketError]::TimedOut) {
                    continue
                }
                throw
            }

            $text = [System.Text.Encoding]::UTF8.GetString($bytes)
            if (-not [string]::IsNullOrEmpty($ExpectedToken)) {
                if ($text.Contains($ExpectedToken)) {
                    return [pscustomobject]@{
                        Name = $ListenerName
                        ProcessId = $PID
                        Bytes = $bytes.Length
                    }
                }
                continue
            }

            try {
                $event = $text | ConvertFrom-Json -ErrorAction Stop
                $result = if ($event.command.accepted) { "accepted" } else { "rejected" }
                # Reports arrive only when a command changes. A non-zero
                # suppressed count is how many identical commands the driver
                # absorbed while the previous one was held.
                $held = ""
                if ($event.suppressed -gt 0) {
                    $held = " (held for {0} identical commands)" -f $event.suppressed
                }
                Write-Host (
                    "[{0:HH:mm:ss.fff}] {1} report #{2} from {3}:{4} {5} - {6}{7}" -f
                    [DateTime]::Now,
                    $ListenerName,
                    $event.sequence,
                    $event.source.host,
                    $event.source.port,
                    $result,
                    $event.detail,
                    $held)
                Write-Host $event.command.payload
            } catch {
                Write-Warning "Received a non-protocol datagram: $text"
            }
        }

        throw "$ListenerName did not receive the validation probe before timeout"
    } finally {
        $client.Dispose()
    }
}

if (-not $Validate) {
    & $receiver $Group $Port $Name "" 0
    exit 0
}

$token = "anyadance-multicast-validation-$([Guid]::NewGuid())"
$jobs = @()
$sender = $null
try {
    for ($index = 1; $index -le $ListenerCount; $index++) {
        $jobParameters = @{
            Name = "anyadance-log-listener-$index"
            ScriptBlock = $receiver
            ArgumentList = @(
                $Group,
                $Port,
                "listener-$index",
                $token,
                $TimeoutSeconds)
        }
        $jobs += Start-Job @jobParameters
    }

    $sender = [System.Net.Sockets.UdpClient]::new(
        [System.Net.Sockets.AddressFamily]::InterNetwork)
    $sender.Client.SetSocketOption(
        [System.Net.Sockets.SocketOptionLevel]::IP,
        [System.Net.Sockets.SocketOptionName]::MulticastInterface,
        [System.Net.IPAddress]::Loopback.GetAddressBytes())
    $sender.Client.SetSocketOption(
        [System.Net.Sockets.SocketOptionLevel]::IP,
        [System.Net.Sockets.SocketOptionName]::MulticastTimeToLive,
        0)

    $probe = @{
        version = 1
        event = "command_processed"
        sequence = 0
        suppressed = 0
        source = @{ host = "127.0.0.1"; port = 0 }
        command = @{
            protocol = "pose_frame"
            bytes = 2
            accepted = $true
            devices = @()
            y_clamped = @()
            payload = "{}"
        }
        detail = $token
    } | ConvertTo-Json -Compress -Depth 5
    $probeBytes = [System.Text.Encoding]::UTF8.GetBytes($probe)

    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        $null = $sender.Send($probeBytes, $probeBytes.Length, $Group, $Port)
        Start-Sleep -Milliseconds 100
        $completed = @($jobs | Where-Object State -eq "Completed").Count
        if ($completed -eq $ListenerCount) {
            break
        }
    }

    $results = @($jobs | Receive-Job -Wait -AutoRemoveJob)
    $jobs = @()
    if ($results.Count -ne $ListenerCount) {
        throw "Expected $ListenerCount receivers, but only $($results.Count) reported success"
    }

    foreach ($result in $results | Sort-Object Name) {
        Write-Host (
            "PASS: {0} (process {1}) received {2} bytes" -f
            $result.Name,
            $result.ProcessId,
            $result.Bytes)
    }
    Write-Host "PASS: all $ListenerCount independent listener processes received the multicast probe"
} finally {
    if ($null -ne $sender) {
        $sender.Dispose()
    }
    foreach ($job in $jobs) {
        Stop-Job $job -ErrorAction SilentlyContinue
        Remove-Job $job -Force -ErrorAction SilentlyContinue
    }
}
