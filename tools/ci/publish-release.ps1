<#
  CI (main only): publishes the installer as GitHub Release v<VERSION>. If that release already
  exists it is replaced with this build. Bump VERSION to start a new release line.
#>
$ErrorActionPreference = 'Stop'
$tag = "v$($env:HEM_VERSION)"
gh release view $tag *> $null
if ($LASTEXITCODE -eq 0) {
    Write-Host "Release $tag exists; replacing it with this build ($env:GITHUB_SHA)."
    gh release delete $tag --yes --cleanup-tag
    if ($LASTEXITCODE -ne 0) { throw "gh release delete failed ($LASTEXITCODE)" }
}
gh release create $tag "$($env:HEM_INSTALLER)" "$($env:HEM_SOURCE_ZIP)" --target "$($env:GITHUB_SHA)" --title "HEM-Elite-Rack $tag" --notes-file installer/RELEASE-NOTES.md
if ($LASTEXITCODE -ne 0) { throw "gh release create failed ($LASTEXITCODE)" }
Write-Host "Published release $tag with $(Split-Path $env:HEM_INSTALLER -Leaf) at $env:GITHUB_SHA"
