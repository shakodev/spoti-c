# Shell/Home/Library completion evidence (#42)

Recorded before completing the remaining shell behavior, 2026-10-20.

The installed Spotify 1.3.5.3058 executable still has SHA-256
`4704c2b7cd76878ab36656f09989cfa132a02f9711d7a8707750a8ff68722136`.
REA opened this exact PE and independently inventoried its 15 resources in
`ev_088790504ba64186937a95d449645d8513b16cc747f1a628593245ca51cfe8b4`.
Static PE resources do not establish live rendering. REA's native UI observation
was attempted against the actual selected PID/window after binding this target.
It returned `capability_unavailable`: this provider's native UI utilities require
macOS. Windows Computer Use is therefore the separately attributed runtime
observer. It now successfully activates and reads the installed Spotify window;
the older unavailable-reference receipt is historical, not a current blocker.

REA parsed the installed extraction's `xpui-routes-profile.js`,
`xpui-routes-your-library-x.js` and `dwp-home-chips-row.js`. Parent Evidence:
`ev_23516f5b8e6c2e06a7d56b309be9ff6a886dc48d00e462f4857d0b1099d8d210`.
The complete summary receipt is adjacent in `shell-runtime-rea.json`.
Three files/217745 bytes parsed without failures; 270 unselected sources and
15949 semantic unknowns remain. Semantic node ceilings make this partial static
evidence. No new endpoint or request is inferred from this analysis.

## Runtime observations

The approved read-only session displayed a populated Library panel independently
of the selected main route (album, then Home). Its header exposes Collapse Your
Library and Expand Your Library, supported filters, Library search and a view
picker. Rows have artwork, title and secondary type/owner text, with separate
navigation and conditional play actions. A resize slider reported 412 in the
observed saved account layout; this is a user state, not a default-width claim.

At the observed 1600x900 window, the black global header occupies approximately
64 physical pixels. Home is a 48-pixel circle adjacent to a rounded search field
in the centered header group. Back/forward sit at the left; notifications,
activity and the circular account avatar sit to the right before caption controls.
The panel gap/radius are visually consistent with the pinned eight-pixel CSS
values. Full viewport/DPI/zoom measurements follow in the comparison receipts.

Home shows a conditional announcement, content chips, compact two-column recent
shortcuts in the observed reduced main width, then titled artwork shelves.
Those personalized sections are unavailable to this client's existing 403
fallback and must not be replaced with fake recommendations.

The account avatar opens a right-aligned elevated menu. The observed entries
include Account, family-plan setup, Profile, Recents, Support, Private session,
Settings and Log out, followed by updates. Reading the menu made no account
change. Private account names/content and raw pixels remain in ignored local
evidence; published records use geometry, state and hashes.

## Completion contract

- Keep the Library sidebar populated across Home/detail/Settings navigation.
  Fetch only the existing real rootlist; own a bounded immutable projection,
  cancel replacement/logout work and reject stale account deliveries. Main-page
  navigation must not invalidate sidebar identities or borrow freed JSON.
- Show supported playlist rows, meaningful cover placeholders and real supplied
  artwork through the existing capped worker/cache. Collapsed rows retain
  artwork targets and keyboard labels. Search/filter/scroll operate on the
  loaded finite page and do not claim account-wide Library search. Unsupported
  entity types remain explicit service limitations.
- Provide the account/avatar menu with working Settings and sign-in/sign-out.
  Profile/account/private-session/activity services absent from this client
  expose clear unavailable reasons, never an inert working-looking action.
  Do not invent a profile endpoint or personalized sections.
- Center the global Home/search group where space permits, keep back and account
  controls reachable at compact widths, and reserve the native caption boundary.
  Profile/menu dismissal, keyboard focus/activation and navigation are tested.
- Preserve merged #44 transport, metadata, consumed-frame clocks and queue
  controls while integrating the #42 shell/components. Keep existing memory caps,
  page/texture ownership, zero warm-frame allocation and blocking event waits.

The completed comparison matrix and native/reference discrepancies are recorded
in [the completion receipt](../verification/shell-home-library-completion.md).
The runtime reference index preserves exact viewports, scale and JPEG hashes;
native readbacks retain physical pixels and independent hashes. No fixture is
labeled as live parity. Reference 200% is excluded under the user's explicit
standard-scaling constraint; native 192-DPI coverage remains separate.
