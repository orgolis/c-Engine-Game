$p = Get-Process editor
$a = @{}
foreach ($t in $p.Threads) { $a[$t.Id] = $t.TotalProcessorTime.TotalMilliseconds }
Start-Sleep -Seconds 10
$p.Refresh()
$rows = foreach ($t in $p.Threads) {
    $d = $t.TotalProcessorTime.TotalMilliseconds - $a[$t.Id]
    [pscustomobject]@{ Id = $t.Id; ms = [math]::Round($d, 0); pct = [math]::Round($d / 100.0, 1); start = $t.StartAddress }
}
$rows | Sort-Object ms -Descending | Select-Object -First 16 | Format-Table -AutoSize | Out-String
"threads: {0}; total {1:N0} ms CPU in 10 s" -f $rows.Count, ($rows | Measure-Object ms -Sum).Sum
