param(
    [Parameter(Mandatory=$true)][string]$ReferenceRoot,
    [string]$Output = (Join-Path $PSScriptRoot 'packaged-observations.json')
)
$ErrorActionPreference='Stop'
$root=(Resolve-Path -LiteralPath $ReferenceRoot).Path
$utf8=[Text.UTF8Encoding]::new($false)
# Supplemental inert byte inspection after REA selected-source analysis.
# This extracts exact rules and bounded snippets; it does not evaluate CSS,
# execute JavaScript, resolve imports or observe account-dependent behavior.
$css=@{
    'xpui-snapshot.css'=@(
        '@property --left-sidebar-width',
        '.LiEjwLua6RNBCvlysAT2{',
        '.yQCyOSVcMCen_NJKPHB6{',
        '.u3xv0vOK9EJmOplXOrhG{',
        '.R1kU6b2KxZc8FWfbYf3m{',
        '.XvuTwlgQRqJ5NnVLN9ZH{',
        '.LayoutResizer__resize-bar{',
        '.Ho539Cb0LwVdYEIDfHCW{',
        '.wPTWE6ubGXmX2aeVyhzW{',
        '.oxFroaXCJTMXfoZDaaf1{',
        '.swoq8h_oER0f7W3oblBE .oxFroaXCJTMXfoZDaaf1{',
        '.forceExpandSearchInput.searchInputCollapsed',
        '.jZZUhUAqjBxjeJnb4GIK{',
        '.IKiPTZqEz15n8iB19aAW{'
    )
    'xpui-routes-your-library-x.css'=@(
        '.kOAVq_5h2_KhR2SmJfaj{',
        '.kOAVq_5h2_KhR2SmJfaj.WhOLDvdymTooJlYiZbNZ{',
        '.kOAVq_5h2_KhR2SmJfaj .cG_EdUD8VD_W0DYHcDlG{',
        '.WhOLDvdymTooJlYiZbNZ .cG_EdUD8VD_W0DYHcDlG{',
        '.AyQuHx4nhILLZ2tOcDiA{',
        '.L4ULnrZfqED8dnsf1kRt{',
        '.E7qovAeNOgz4DhcnQ1J2{',
        '.Yqfut8Aw1vohHPWNRsWy{',
        '.Y3i0QVby7QVDsOh2l0_m{',
        '.Y3i0QVby7QVDsOh2l0_m:has(:focus-visible){',
        '.xNN4u6E5VGbOxYTpOp6X:hover .Y3i0QVby7QVDsOh2l0_m{',
        '.FmKJWwZAE3hskdmEk_qu:not(.g17mZo3hN5ItSnNz4xGg){',
        '.WAc4pJu2EshSMM7cQK9c{',
        '.FmKJWwZAE3hskdmEk_qu:hover .WAc4pJu2EshSMM7cQK9c,',
        '.FaCi3LjHTjAbSuSnegTN{',
        '.qB0QkhexXi3Wx0P1k0Rf{',
        '.RD7Pn8_nYXLoBOaIbdvc{',
        '.RD7Pn8_nYXLoBOaIbdvc:has(',
        '.eIMqq7JyNeNCDtjFSlFQ{',
        '.eIMqq7JyNeNCDtjFSlFQ li{',
        '.BKSy1FhNKrGmUPkur9gp{'
    )
    'dwp-home-header.css'=@('.kMaYBahSBLOnErF7DLR2{','.kMaYBahSBLOnErF7DLR2.ERB9Yr1OyWiEbIozlRtA{')
    'dwp-home-chips-row.css'=@('.MqAdcwpMRukiKp3i8Fxr{')
}
$js=@(
    @{file='dwp-top-bar.js';needle='let h=[';length=780;topic='Route content header exclusions, not global navigation'},
    @{file='dwp-home-header.js';needle='HomeHeader:';length=400;topic='Dynamic Home background and conditional hidden class'},
    @{file='dwp-home-chips-row.js';needle='initialSelectedChip:';length=650;topic='Selected/open chip state'},
    @{file='home-v2-section.js';needle='let b=({section:e})';length=850;topic='Generic and low-density section branches'},
    @{file='home-v2-section.js';needle='let T=10,';length=12;topic='Full-page section skeleton count, not shelf column count'},
    @{file='xpui-routes-your-library-x.js';needle='M=eI?"collapsed"';length=335;topic='Collapsed plus compact/default list/grid variants'},
    @{file='xpui-routes-your-library-x.js';needle='let aQ=({isCollapsed';length=1000;topic='Library collapse button, action and accessible names'},
    @{file='xpui-routes-your-library-x.js';needle='images:p(r,48)';length=420;topic='48px row artwork with circle/rounded shape and loading input'},
    @{file='xpui-routes-your-library-x.js';needle='isCollapsed:g}';length=320;topic='Row play overlay capability and collapse condition'},
    @{file='xpui-routes-your-library-x.js';needle='"sort-and-view-picker"';length=380;topic='Sort/view picker state'},
    @{file='xpui-routes-your-library-x.js';needle='estimatedItemMaxHeight:250';length=75;topic='Virtual grid estimated height, not a fixed card height'},
    @{file='dwp-offline-empty-state.js';needle='OfflineEmptyState:({showDownloadInformation';length=690;topic='Offline title, subtitle and download-information alternative'},
    @{file='dwp-page-error-template.js';needle='e=(0,a.useCallback)(()=>{t({type:"hitRefreshContent"})';length=230;topic='Error template refresh action and button label'}
)
$identities=@()
$rules=@()
foreach($name in ($css.Keys | Sort-Object)){
    $path=Join-Path $root $name
    $text=[IO.File]::ReadAllText($path)
    $identities+=@{path=$name;bytes=(Get-Item -LiteralPath $path).Length;sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()}
    $leaf=[regex]::Matches($text,'[^{}]+\{[^{}]*\}')
    foreach($needle in $css[$name]){
        $matched=@($leaf | Where-Object { $_.Value.StartsWith($needle,[StringComparison]::Ordinal) })
        if(!$matched.Count){throw "Missing CSS selector: $name $needle"}
        foreach($m in $matched){
            $before=[Math]::Max(0,$m.Index-100)
            $rules+=@{path=$name;byte_offset=$utf8.GetByteCount($text.Substring(0,$m.Index));
                rule=$m.Value;context_before=$text.Substring($before,$m.Index-$before)}
        }
    }
}
$snippets=@()
foreach($row in $js){
    $path=Join-Path $root $row.file
    $text=[IO.File]::ReadAllText($path)
    if(!($identities | Where-Object {$_.path -eq $row.file})){
        $identities+=@{path=$row.file;bytes=(Get-Item -LiteralPath $path).Length;sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()}
    }
    $index=$text.IndexOf($row.needle,[StringComparison]::Ordinal)
    if($index -lt 0){throw "Missing JavaScript marker: $($row.file) $($row.needle)"}
    $length=[Math]::Min($row.length,$text.Length-$index)
    $snippets+=@{path=$row.file;topic=$row.topic;byte_offset=$utf8.GetByteCount($text.Substring(0,$index));
        snippet=$text.Substring($index,$length)}
}
$record=@{
    method='Supplemental inert literal/rule extraction; exact UTF-8 offsets, no execution or cascade evaluation'
    rea_parent='ev_591ff2bc2f44e77aeb27efdf4c6d23eca9c048aa0a27f332ecffbc9cbc1c05ed'
    source_root=$root
    identities=$identities
    css_rules=$rules
    javascript_snippets=$snippets
    limits=@('Selectors can have conditional ancestors; retain context_before and consult original source.',
        'Module imports and feature flags can change placement, density and active variants.',
        'No window, account, pointer, keyboard, GPU or runtime observation.')
}
[IO.File]::WriteAllText([IO.Path]::GetFullPath($Output),($record|ConvertTo-Json -Depth 12),$utf8)
