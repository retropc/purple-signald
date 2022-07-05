#include "libsignald.h"

PurpleGroup * signald_get_purple_group() {
    PurpleGroup *group = purple_blist_find_group("Signal");
    if (!group) {
        group = purple_group_new("Signal");
        purple_blist_add_group(group, NULL);
    }
    return group;
}

/*
 * Add group chat to blist. Updates existing group chat if found.
 */
PurpleChat * signald_ensure_group_chat_in_blist(
    PurpleAccount *account, const char *groupId, const char *title
) {
    gboolean fetch_contacts = TRUE;

    PurpleChat *chat = purple_blist_find_chat(account, groupId);

    if (chat == NULL && fetch_contacts) {
        GHashTable *comp = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, g_free);
        g_hash_table_insert(comp, "name", g_strdup(groupId));
        chat = purple_chat_new(account, groupId, comp);
        PurpleGroup *group = signald_get_purple_group();
        purple_blist_add_chat(chat, group, NULL);
    }

    if (title != NULL && fetch_contacts) {
        // components uses free on key (unlike above)
        purple_blist_alias_chat(chat, title);
    }

    return chat;
}

/*
 * Given a JsonNode for a group member, get the uuid.
 */
char *
signald_get_group_member_uuid(JsonNode *node)
{
    JsonObject *member = json_node_get_object(node);
    return (char *)json_object_get_string_member(member, "uuid");
}

/*
 * Given a list of members, get back the list of corresponding uuid.
 * This function makes a copy of the number so it must be freed.
 */
GList *
signald_members_to_uuids(JsonArray *members)
{
    GList *uuids = NULL;

    for (
        GList *this_member = json_array_get_elements(members);
        this_member != NULL;
        this_member = this_member->next
    ) {
        JsonNode *element = (JsonNode *)(this_member->data);
        char *uuid = signald_get_group_member_uuid(element);
        uuids = g_list_append(uuids, g_strdup(uuid));
    }
    return uuids;
}

gboolean
signald_members_contains_uuid(JsonArray *members, char *uuid)
{
    GList *uuids = signald_members_to_uuids(members);
    gboolean result = g_list_find_custom(uuids, uuid, (GCompareFunc)g_strcmp0) != NULL;
    g_list_free_full(uuids, g_free);
    return result;
}

/*
 * Functions to manipulate our groups.
 */

/*
 * Function to add a set of users to a Pidgin conversation.
 * The main logic here is setting up the flags appropriately.
 */
 /*
void
signald_add_users_to_conv(SignaldAccount *sa, SignaldGroup *group, GList *users)
{
    GList *flags = NULL;
    for(GList *user = users; user != NULL; user = user->next) {
        char * uuid = user->data;
        purple_debug_info(SIGNALD_PLUGIN_ID, "%s is member of %s.\n", uuid, group->name);
        PurpleBuddy * buddy = purple_find_buddy(sa->account, uuid);
        flags = g_list_append(flags, GINT_TO_POINTER(PURPLE_CBFLAGS_NONE));
        if (buddy) {
            user->data = g_strdup((gpointer)purple_buddy_get_name(buddy));
        }
    }
    purple_chat_conversation_add_users(PURPLE_CONV_CHAT(group->conversation), users, NULL, flags, FALSE);
    g_list_free(flags);
}
*/

/*
 * Given a group ID, open up an actual conversation for the group.  This is
 * ultimately what opens the chat window, channel, etc.
 */
void
signald_open_conversation(SignaldAccount *sa, const char *groupId)
{
//TODO
/*
    SignaldGroup *group = (SignaldGroup *)g_hash_table_lookup(sa->groups, groupId);

    if (group == NULL) {
        return;
    }

    if (group->conversation != NULL) {
        return;
    }

    // This is the magic that triggers the chat to actually open.
    group->conversation = serv_got_joined_chat(sa->pc, group->id, group->name);

    purple_conv_chat_set_topic(PURPLE_CONV_CHAT(group->conversation), group->name, group->name);

    // Squirrel away the group ID as part of the conversation for easy access later.
    purple_conversation_set_data(group->conversation, SIGNALD_CONV_GROUPID_KEY, g_strdup(groupId));

    // Populate the channel user list.
    signald_add_users_to_conv(sa, group, group->users);
*/
}

void
signald_accept_groupV2_invitation(SignaldAccount *sa, const char *groupId, JsonArray *pendingMembers)
{
    g_return_if_fail(sa->uuid);

    // See if we're a pending member of the group v2.
    gboolean pending = signald_members_contains_uuid(pendingMembers, sa->uuid);
    if (pending) {
        // we are a pending member, join it
        JsonObject *message = json_object_new();
        json_object_set_string_member(message, "type", "accept_invitation");
        json_object_set_string_member(message, "account", sa->uuid);
        json_object_set_string_member(message, "groupID", groupId);
        if (!signald_send_json(sa, message)) {
            purple_connection_error(sa->pc, PURPLE_CONNECTION_ERROR_NETWORK_ERROR, _("Could not send message for accepting group invitation."));
        }
        json_object_unref(message);
    }
}

/*
 * Determines if user is participating in conversation.
 */
int signald_user_in_conv_chat(PurpleConvChat *conv_chat, const char *uuid) {
    for (GList *users = purple_conv_chat_get_users(conv_chat); users != NULL;
        users = users->next) {
        PurpleConvChatBuddy *buddy = (PurpleConvChatBuddy *) users->data;
        if (!strcmp(buddy->name, uuid)) {
            return TRUE;
        }
    }
    return FALSE;
}

/*
 * This handles incoming Signal group information,
 * adding participants to all chats currently active.
 */
void
signald_chat_add_participants(PurpleAccount *account, const char *groupId, JsonArray *members) {
    GList *uuids = signald_members_to_uuids(members);
    PurpleConvChat *conv_chat = purple_conversations_find_chat_with_account(groupId, account);
    if (conv_chat != NULL) { // only consider active chats
        for (GList * uuid_elem = uuids; uuid_elem != NULL; uuid_elem = uuid_elem->next) {
            const char* uuid = uuid_elem->data;
            if (!signald_user_in_conv_chat(conv_chat, uuid)) {
                PurpleConvChatBuddyFlags flags = 0;
                purple_conv_chat_add_user(conv_chat, uuid, NULL, flags, FALSE);
            }
        }
    }
    g_list_free_full(uuids, g_free);
}

void
signald_process_groupV2_obj(SignaldAccount *sa, JsonObject *obj)
{
    const char *groupId = json_object_get_string_member(obj, "id");
    const char *title = json_object_get_string_member(obj, "title");
    purple_debug_info (SIGNALD_PLUGIN_ID, "Processing group ID %s, %s\n",
                       groupId,
                       title);

    if (purple_account_get_bool(sa->account, "auto-accept-invitations", FALSE)) {
        signald_accept_groupV2_invitation(sa,
            groupId,
            json_object_get_array_member(obj, "pendingMembers")
        );
    }

    signald_ensure_group_chat_in_blist(sa->account, groupId, title);
    signald_chat_add_participants(sa->account, groupId, json_object_get_array_member(obj, "members"));
}

void
signald_process_groupV2(JsonArray *array, guint index_, JsonNode *element_node, gpointer user_data)
{
    SignaldAccount *sa = (SignaldAccount *)user_data;
    JsonObject *obj = json_node_get_object(element_node);
    signald_process_groupV2_obj(sa, obj);
}

void
signald_parse_groupV2_list(SignaldAccount *sa, JsonArray *groups)
{
    json_array_foreach_element(groups, signald_process_groupV2, sa);
}

void
signald_request_group_info(SignaldAccount *sa, const char *groupId)
{
    g_return_if_fail(sa->uuid);

    JsonObject *data = json_object_new();

    json_object_set_string_member(data, "type", "get_group");
    json_object_set_string_member(data, "account", sa->uuid);
    json_object_set_string_member(data, "groupID", groupId);

    if (!signald_send_json(sa, data)) {
        purple_connection_error(sa->pc, PURPLE_CONNECTION_ERROR_NETWORK_ERROR, _("Could not request group info."));
    }

    json_object_unref(data);
}

void
signald_request_group_list(SignaldAccount *sa)
{
    g_return_if_fail(sa->uuid);

    JsonObject *data = json_object_new();

    json_object_set_string_member(data, "type", "list_groups");
    json_object_set_string_member(data, "account", sa->uuid);

    if (!signald_send_json(sa, data)) {
        purple_connection_error(sa->pc, PURPLE_CONNECTION_ERROR_NETWORK_ERROR, _("Could not request groups."));
    }

    json_object_unref(data);
}

PurpleConversation * signald_enter_group_chat(PurpleConnection *pc, const char *groupId) {
    // use hash of groupId for chat id number
    PurpleConversation *conv = purple_find_chat(pc, g_str_hash(groupId));
    if (conv == NULL) {
        conv = serv_got_joined_chat(pc, g_str_hash(groupId), groupId);
        purple_conversation_set_data(conv, "name", g_strdup(groupId));
        SignaldAccount *sa = purple_connection_get_protocol_data(pc);
        signald_request_group_info(sa, groupId);
    }
    return conv;
}

/*
 * Process a message we've received from signald that's directed at a group
 * v2 chat.
 *
 * Can only receive messages, no administrative functions are implemented.
 */
void
signald_process_groupV2_message(SignaldAccount *sa, SignaldMessage *msg)
{
    JsonObject *groupInfo = json_object_get_object_member(msg->data, "groupV2");
    const gchar *groupId = json_object_get_string_member(groupInfo, "id");

    PurpleConversation * conv = signald_enter_group_chat(sa->pc, groupId);

    PurpleMessageFlags flags = 0;
    gboolean has_attachment = FALSE;
    GString *content = NULL;
    if (signald_format_message(sa, msg, &content, &has_attachment)) {
        if (has_attachment) {
            flags |= PURPLE_MESSAGE_IMAGES;
        }
        if (msg->is_sync_message) {
            flags |= PURPLE_MESSAGE_SEND | PURPLE_MESSAGE_REMOTE_SEND | PURPLE_MESSAGE_DELAYED;
        } else {
            flags |= PURPLE_MESSAGE_RECV;
        }
        purple_debug_info(SIGNALD_PLUGIN_ID, "calling purple_conv_chat_write(…)…\n");
        purple_conv_chat_write(PURPLE_CONV_CHAT(conv), msg->sender_uuid, content->str, flags, msg->timestamp);
    } else {
        purple_debug_warning(SIGNALD_PLUGIN_ID, "signald_format_message returned false.\n");
    }
    g_string_free(content, TRUE);
}

int
signald_send_chat(PurpleConnection *pc, int id, const char *message, PurpleMessageFlags flags)
{
    purple_debug_info(SIGNALD_PLUGIN_ID, "signald_send_chat…\n");
    SignaldAccount *sa = purple_connection_get_protocol_data(pc);
    PurpleConversation *conv = purple_find_chat(pc, id);
    purple_debug_info(SIGNALD_PLUGIN_ID, "conv = %p\n", conv);
    if (conv != NULL) {
        gchar *groupId = (gchar *)purple_conversation_get_data(conv, "name");
        purple_debug_info(SIGNALD_PLUGIN_ID, "groupId = %p\n", groupId);
        if (groupId != NULL) {
            int ret = signald_send_message(sa, SIGNALD_MESSAGE_TYPE_GROUPV2, groupId, message);
            if (ret > 0) {
                // immediate local echo (ret == 0 indicates delayed local echo)
                purple_conversation_write(conv, sa->uuid, message, flags, time(NULL));
            }
            return ret;
        }
        return -6; // a negative value to indicate failure. chose ENXIO "no such address"
    }
    return -6; // a negative value to indicate failure. chose ENXIO "no such address"
}

void
signald_join_chat(PurpleConnection *pc, GHashTable *data)
{
    const char *groupId = g_hash_table_lookup(data, "name");
    if (groupId != NULL) {
        // add chat to buddy list (optional)
        //PurpleAccount *account = purple_connection_get_account(pc);
        //const char *topic = g_hash_table_lookup(data, "topic");
        //gowhatsapp_ensure_group_chat_in_blist(account, groupId, topic);
        // create conversation (important)
        signald_enter_group_chat(pc, groupId);
    }
}

/*
 * Functions to supply Pidgin with metadata about this plugin.
 */
GList *
signald_chat_info(PurpleConnection *pc)
{
    GList *infos = NULL;

    struct proto_chat_entry *pce;

    pce = g_new0(struct proto_chat_entry, 1);
    pce->label = _("_Group Name:");
    pce->identifier = "name";
    pce->required = TRUE;
    infos = g_list_append(infos, pce);

    return infos;
}

GHashTable
*signald_chat_info_defaults(PurpleConnection *pc, const char *chat_name)
{
    GHashTable *defaults;

    defaults = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, g_free);

    if (chat_name != NULL) {
        g_hash_table_insert(defaults, "name", g_strdup(chat_name));
    }

    return defaults;
}

void
signald_set_chat_topic(PurpleConnection *pc, int id, const char *topic)
{
    // Nothing to do here. For some reason this callback has to be
    // registered if Pidgin is going to enable the "Alias..." menu
    // option in the conversation.
}

/*
 * Get the identifying aspect of a chat (as passed to serv_got_joined_chat)
 * given the chat_info entries. In Signal, this is the groupId.
 *
 * Borrowed from:
 * https://github.com/matrix-org/purple-matrix/blob/master/libmatrix.c
 */
char *signald_get_chat_name(GHashTable *components)
{
    const char *groupId = g_hash_table_lookup(components, "name");
    return g_strdup(groupId);
}
