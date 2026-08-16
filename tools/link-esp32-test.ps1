param(
    [string]$Esp32TestRoot = "..\esp32_test"
)

$ErrorActionPreference = "Stop"

$projectRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$sourceRoot = Resolve-Path (Join-Path $projectRoot $Esp32TestRoot)
$externalRoot = Join-Path $projectRoot "external"

New-Item -ItemType Directory -Force -Path $externalRoot | Out-Null

function New-LocalJunction {
    param(
        [string]$Name,
        [string]$Target
    )

    $linkPath = Join-Path $externalRoot $Name
    $targetPath = Resolve-Path (Join-Path $sourceRoot $Target)

    if (Test-Path $linkPath) {
        $item = Get-Item $linkPath
        if (-not ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw "$linkPath exists and is not a link. Move it before relinking."
        }
        Remove-Item -LiteralPath $linkPath -Force
    }

    New-Item -ItemType Junction -Path $linkPath -Target $targetPath | Out-Null
    Write-Host "linked $Name -> $targetPath"
}

New-LocalJunction -Name "ecosystem_protocol" -Target "lib\ecosystem_protocol"
New-LocalJunction -Name "cardputer_controller_reference" -Target "controllers\cardputer"
