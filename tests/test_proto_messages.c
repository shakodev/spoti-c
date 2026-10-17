#include "proto_test.h"
#include "authentication.pb.h"
#include "keyexchange.pb.h"
#include "mercury.pb.h"
#include "metadata.pb.h"
#include "connect.pb.h"
#include "transfer_state.pb.h"
#include "storage-resolve.pb.h"
#include "spotify/clienttoken/v0/clienttoken_http.pb.h"
#include "spotify/login5/v3/login5.pb.h"

static void authentication(void)
{
    /* Full AP login request: required nested messages and system identity. */
    static const uint8_t encrypted[] = {
        0x52,10,0x52,1,'u',0xa0,1,3,0xf2,1,1,'t',
        0x92,3,5,0x50,2,0xe0,3,1
    };
    ClientResponseEncrypted response = ClientResponseEncrypted_init_zero;
    CHECK(proto_decode(ClientResponseEncrypted_fields, encrypted, sizeof(encrypted), &response) == PROTO_OK);
    CHECK(response.system_info.cpu_family == CpuFamily_CPU_X86_64);
    CHECK(response.system_info.os == Os_OS_WINDOWS);
    golden(ClientResponseEncrypted_fields, &response, encrypted, sizeof(encrypted));
    static const uint8_t wire[] = {0x52,1,'u',0xa0,1,3,0xf2,1,1,'t'};
    LoginCredentials value = LoginCredentials_init_zero;
    CHECK(proto_decode(LoginCredentials_fields, wire, sizeof(wire), &value) == PROTO_OK);
    CHECK(value.has_username && strcmp(value.username, "u") == 0);
    CHECK(value.typ == AuthenticationType_AUTHENTICATION_SPOTIFY_TOKEN);
    CHECK(value.has_auth_data && value.auth_data.size == 1 && value.auth_data.bytes[0] == 't');
    golden(LoginCredentials_fields, &value, wire, sizeof(wire));
    static const uint8_t challenge[] = {0x52,1,1,0xa0,1,0,0xf2,1,2,0xaa,0xbb};
    LoginCryptoDiffieHellmanChallenge dh = LoginCryptoDiffieHellmanChallenge_init_zero;
    CHECK(proto_decode(LoginCryptoDiffieHellmanChallenge_fields, challenge, sizeof(challenge), &dh) == PROTO_OK);
    CHECK(dh.gs_signature.size == 2 && dh.gs_signature.bytes[1] == 0xbb);
    golden(LoginCryptoDiffieHellmanChallenge_fields, &dh, challenge, sizeof(challenge));
    CHECK(proto_decode(LoginCredentials_fields, NULL, 0, &value) == PROTO_MALFORMED);
}

static void mercury(void)
{
    static const uint8_t wire[] = {0x0a,6,'h','m',':','/','/','x',0x1a,3,'G','E','T'};
    Header value = Header_init_zero;
    CHECK(proto_decode(Header_fields, wire, sizeof(wire), &value) == PROTO_OK);
    CHECK(value.has_uri && strcmp(value.uri, "hm://x") == 0);
    CHECK(value.has_method && strcmp(value.method, "GET") == 0);
    golden(Header_fields, &value, wire, sizeof(wire));
}

static bool read_name(pb_istream_t *stream, const pb_field_t *field, void **arg)
{
    char *name = *arg;
    (void)field;
    if (stream->bytes_left != 1) return false;
    return pb_read(stream, (uint8_t *)name, 1);
}

static void metadata(void)
{
    static const uint8_t named[] = {0x12,1,'R'};
    char name[2] = {0};
    spotify_metadata_Artist artist = spotify_metadata_Artist_init_zero;
    artist.name.funcs.decode = read_name; artist.name.arg = name;
    CHECK(proto_decode(spotify_metadata_Artist_fields, named, sizeof(named), &artist) == PROTO_OK);
    CHECK(strcmp(name, "R") == 0);
    ProtoSlice slice = {(const uint8_t *)"R", 1};
    artist.name.funcs.encode = emit_slice; artist.name.arg = &slice;
    golden(spotify_metadata_Artist_fields, &artist, named, sizeof(named));
    spotify_metadata_Album album = spotify_metadata_Album_init_zero;
    name[0] = 0;
    album.name.funcs.decode = read_name; album.name.arg = name;
    CHECK(proto_decode(spotify_metadata_Album_fields, named, sizeof(named), &album) == PROTO_OK);
    CHECK(strcmp(name, "R") == 0);
    album.name.funcs.encode = emit_slice; album.name.arg = &slice;
    golden(spotify_metadata_Album_fields, &album, named, sizeof(named));
    spotify_metadata_Track track = spotify_metadata_Track_init_zero;
    track.name.funcs.decode = read_name; track.name.arg = name;
    CHECK(proto_decode(spotify_metadata_Track_fields, named, sizeof(named), &track) == PROTO_OK);
    track.name.funcs.encode = emit_slice; track.name.arg = &slice;
    golden(spotify_metadata_Track_fields, &track, named, sizeof(named));
    spotify_metadata_Restriction restriction = spotify_metadata_Restriction_init_zero;
    static const uint8_t countries[] = {0x12,2,'U','S',0x1a,2,'N','L'};
    CHECK(proto_decode(spotify_metadata_Restriction_fields, countries, sizeof(countries), &restriction) == PROTO_OK);
    CHECK(restriction.which_country_restriction == spotify_metadata_Restriction_countries_forbidden_tag);
    CHECK(strcmp(restriction.country_restriction.countries_forbidden, "NL") == 0);
    golden(spotify_metadata_Restriction_fields, &restriction, countries + 4, 4);
}

static void connect_and_transfer(void)
{
    static const uint8_t wire[] = {0x12,5,0x0a,3,0x1a,1,'D',0x20,1,0x30,7};
    spotify_connectstate_PutStateRequest value = spotify_connectstate_PutStateRequest_init_zero;
    CHECK(proto_decode(spotify_connectstate_PutStateRequest_fields, wire, sizeof(wire), &value) == PROTO_OK);
    CHECK(value.is_active && value.message_id == 7 && value.has_device);
    CHECK(value.device.has_device_info && strcmp(value.device.device_info.name, "D") == 0);
    golden(spotify_connectstate_PutStateRequest_fields, &value, wire, sizeof(wire));
    static const uint8_t transfer[] = {0x12,4,0x10,0x7b,0x20,1,0x22,7,0x0a,3,0x0a,1,'u',0x10,1};
    spotify_player_proto_transfer_TransferState state = spotify_player_proto_transfer_TransferState_init_zero;
    CHECK(proto_decode(spotify_player_proto_transfer_TransferState_fields, transfer, sizeof(transfer), &state) == PROTO_OK);
    CHECK(state.has_playback && state.playback.position_as_of_timestamp == 123);
    CHECK(state.playback.is_paused && state.queue.is_playing_queue);
    ProtoSlice track = {(const uint8_t *)"\x0a\x01u",3};
    state.queue.tracks.funcs.encode = emit_slice; state.queue.tracks.arg = &track;
    golden(spotify_player_proto_transfer_TransferState_fields, &state, transfer, sizeof(transfer));
}

static void tokens(void)
{
    static const uint8_t wire[] = {8,1,0x12,3,0x0a,1,'v'};
    static spotify_clienttoken_http_v0_ClientTokenRequest token;
    CHECK(proto_decode(spotify_clienttoken_http_v0_ClientTokenRequest_fields, wire, sizeof(wire), &token) == PROTO_OK);
    CHECK(token.which_request == spotify_clienttoken_http_v0_ClientTokenRequest_client_data_tag);
    CHECK(strcmp(token.request.client_data.client_version, "v") == 0);
    golden(spotify_clienttoken_http_v0_ClientTokenRequest_fields, &token, wire, sizeof(wire));
    static const uint8_t grant[] = {8,1,0x12,7,0x0a,1,'t',0x10,60,0x18,30};
    static spotify_clienttoken_http_v0_ClientTokenResponse granted;
    CHECK(proto_decode(spotify_clienttoken_http_v0_ClientTokenResponse_fields, grant, sizeof(grant), &granted) == PROTO_OK);
    CHECK(granted.which_response == spotify_clienttoken_http_v0_ClientTokenResponse_granted_token_tag);
    CHECK(strcmp(granted.response.granted_token.token, "t") == 0);
    CHECK(granted.response.granted_token.expires_after_seconds == 60);
    golden(spotify_clienttoken_http_v0_ClientTokenResponse_fields, &granted, grant, sizeof(grant));
    static const uint8_t login[] = {0xa2,6,6,0x0a,1,'u',0x12,1,'s'};
    static spotify_login5_v3_LoginRequest request;
    CHECK(proto_decode(spotify_login5_v3_LoginRequest_fields, login, sizeof(login), &request) == PROTO_OK);
    CHECK(request.which_login_method == spotify_login5_v3_LoginRequest_stored_credential_tag);
    CHECK(strcmp(request.login_method.stored_credential.username, "u") == 0);
    CHECK(request.login_method.stored_credential.data.size == 1);
    golden(spotify_login5_v3_LoginRequest_fields, &request, login, sizeof(login));
    static const uint8_t response[] = {0x0a,3,0x0a,1,'u',0x10,1};
    static spotify_login5_v3_LoginResponse reply;
    CHECK(proto_decode(spotify_login5_v3_LoginResponse_fields, response, sizeof(response), &reply) == PROTO_OK);
    CHECK(reply.which_response == spotify_login5_v3_LoginResponse_error_tag);
    CHECK(reply.response.error == spotify_login5_v3_LoginError_INVALID_CREDENTIALS);
    static const uint8_t last[] = {0x10,1};
    golden(spotify_login5_v3_LoginResponse_fields, &reply, last, sizeof(last));
    /* Reverse order: the final submessage must replace the earlier scalar. */
    static const uint8_t reverse[] = {0x10,1,0x0a,3,0x0a,1,'u'};
    memset(&reply, 0, sizeof(reply));
    CHECK(proto_decode(spotify_login5_v3_LoginResponse_fields, reverse, sizeof(reverse), &reply) == PROTO_OK);
    CHECK(reply.which_response == spotify_login5_v3_LoginResponse_ok_tag);
    CHECK(strcmp(reply.response.ok.username, "u") == 0);
}

static void storage(void)
{
    static const uint8_t wire[] = {8,3,0x12,1,'u',0x22,2,1,2};
    spotify_download_proto_StorageResolveResponse value = spotify_download_proto_StorageResolveResponse_init_zero;
    char decoded_url[2] = {0};
    value.cdnurl.funcs.decode = read_name; value.cdnurl.arg = decoded_url;
    CHECK(proto_decode(spotify_download_proto_StorageResolveResponse_fields, wire, sizeof(wire), &value) == PROTO_OK);
    CHECK(value.result == spotify_download_proto_StorageResolveResponse_Result_RESTRICTED);
    CHECK(value.fileid.size == 2 && value.fileid.bytes[1] == 2);
    CHECK(strcmp(decoded_url, "u") == 0);
    ProtoSlice url = {(const uint8_t *)"u",1};
    value.cdnurl.funcs.encode = emit_slice; value.cdnurl.arg = &url;
    golden(spotify_download_proto_StorageResolveResponse_fields, &value, wire, sizeof(wire));
}

void test_proto_messages(void)
{
    authentication(); mercury(); metadata(); connect_and_transfer(); tokens(); storage();
}
