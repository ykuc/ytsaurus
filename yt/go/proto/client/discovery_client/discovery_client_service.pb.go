// Code generated by protoc-gen-go. DO NOT EDIT.
// versions:
// 	protoc-gen-go v1.31.0
// 	protoc        v3.21.3
// source: yt/yt_proto/yt/client/discovery_client/proto/discovery_client_service.proto

package discovery_client

import (
	ytree "go.ytsaurus.tech/yt/go/proto/core/ytree"
	protoreflect "google.golang.org/protobuf/reflect/protoreflect"
	protoimpl "google.golang.org/protobuf/runtime/protoimpl"
	reflect "reflect"
	sync "sync"
)

const (
	// Verify that this generated code is sufficiently up-to-date.
	_ = protoimpl.EnforceVersion(20 - protoimpl.MinVersion)
	// Verify that runtime/protoimpl is sufficiently up-to-date.
	_ = protoimpl.EnforceVersion(protoimpl.MaxVersion - 20)
)

type TMemberInfo struct {
	state         protoimpl.MessageState
	sizeCache     protoimpl.SizeCache
	unknownFields protoimpl.UnknownFields

	Id         *string                     `protobuf:"bytes,1,req,name=id" json:"id,omitempty"`
	Priority   *int64                      `protobuf:"varint,2,req,name=priority" json:"priority,omitempty"`
	Revision   *int64                      `protobuf:"varint,3,req,name=revision" json:"revision,omitempty"`
	Attributes *ytree.TAttributeDictionary `protobuf:"bytes,4,opt,name=attributes" json:"attributes,omitempty"`
}

func (x *TMemberInfo) Reset() {
	*x = TMemberInfo{}
	if protoimpl.UnsafeEnabled {
		mi := &file_yt_yt_proto_yt_client_discovery_client_proto_discovery_client_service_proto_msgTypes[0]
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		ms.StoreMessageInfo(mi)
	}
}

func (x *TMemberInfo) String() string {
	return protoimpl.X.MessageStringOf(x)
}

func (*TMemberInfo) ProtoMessage() {}

func (x *TMemberInfo) ProtoReflect() protoreflect.Message {
	mi := &file_yt_yt_proto_yt_client_discovery_client_proto_discovery_client_service_proto_msgTypes[0]
	if protoimpl.UnsafeEnabled && x != nil {
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		if ms.LoadMessageInfo() == nil {
			ms.StoreMessageInfo(mi)
		}
		return ms
	}
	return mi.MessageOf(x)
}

// Deprecated: Use TMemberInfo.ProtoReflect.Descriptor instead.
func (*TMemberInfo) Descriptor() ([]byte, []int) {
	return file_yt_yt_proto_yt_client_discovery_client_proto_discovery_client_service_proto_rawDescGZIP(), []int{0}
}

func (x *TMemberInfo) GetId() string {
	if x != nil && x.Id != nil {
		return *x.Id
	}
	return ""
}

func (x *TMemberInfo) GetPriority() int64 {
	if x != nil && x.Priority != nil {
		return *x.Priority
	}
	return 0
}

func (x *TMemberInfo) GetRevision() int64 {
	if x != nil && x.Revision != nil {
		return *x.Revision
	}
	return 0
}

func (x *TMemberInfo) GetAttributes() *ytree.TAttributeDictionary {
	if x != nil {
		return x.Attributes
	}
	return nil
}

type TGroupMeta struct {
	state         protoimpl.MessageState
	sizeCache     protoimpl.SizeCache
	unknownFields protoimpl.UnknownFields

	MemberCount *int32 `protobuf:"varint,1,req,name=member_count,json=memberCount" json:"member_count,omitempty"`
}

func (x *TGroupMeta) Reset() {
	*x = TGroupMeta{}
	if protoimpl.UnsafeEnabled {
		mi := &file_yt_yt_proto_yt_client_discovery_client_proto_discovery_client_service_proto_msgTypes[1]
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		ms.StoreMessageInfo(mi)
	}
}

func (x *TGroupMeta) String() string {
	return protoimpl.X.MessageStringOf(x)
}

func (*TGroupMeta) ProtoMessage() {}

func (x *TGroupMeta) ProtoReflect() protoreflect.Message {
	mi := &file_yt_yt_proto_yt_client_discovery_client_proto_discovery_client_service_proto_msgTypes[1]
	if protoimpl.UnsafeEnabled && x != nil {
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		if ms.LoadMessageInfo() == nil {
			ms.StoreMessageInfo(mi)
		}
		return ms
	}
	return mi.MessageOf(x)
}

// Deprecated: Use TGroupMeta.ProtoReflect.Descriptor instead.
func (*TGroupMeta) Descriptor() ([]byte, []int) {
	return file_yt_yt_proto_yt_client_discovery_client_proto_discovery_client_service_proto_rawDescGZIP(), []int{1}
}

func (x *TGroupMeta) GetMemberCount() int32 {
	if x != nil && x.MemberCount != nil {
		return *x.MemberCount
	}
	return 0
}

type TListMembersOptions struct {
	state         protoimpl.MessageState
	sizeCache     protoimpl.SizeCache
	unknownFields protoimpl.UnknownFields

	Limit         *int32   `protobuf:"varint,1,req,name=limit" json:"limit,omitempty"`
	AttributeKeys []string `protobuf:"bytes,2,rep,name=attribute_keys,json=attributeKeys" json:"attribute_keys,omitempty"`
}

func (x *TListMembersOptions) Reset() {
	*x = TListMembersOptions{}
	if protoimpl.UnsafeEnabled {
		mi := &file_yt_yt_proto_yt_client_discovery_client_proto_discovery_client_service_proto_msgTypes[2]
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		ms.StoreMessageInfo(mi)
	}
}

func (x *TListMembersOptions) String() string {
	return protoimpl.X.MessageStringOf(x)
}

func (*TListMembersOptions) ProtoMessage() {}

func (x *TListMembersOptions) ProtoReflect() protoreflect.Message {
	mi := &file_yt_yt_proto_yt_client_discovery_client_proto_discovery_client_service_proto_msgTypes[2]
	if protoimpl.UnsafeEnabled && x != nil {
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		if ms.LoadMessageInfo() == nil {
			ms.StoreMessageInfo(mi)
		}
		return ms
	}
	return mi.MessageOf(x)
}

// Deprecated: Use TListMembersOptions.ProtoReflect.Descriptor instead.
func (*TListMembersOptions) Descriptor() ([]byte, []int) {
	return file_yt_yt_proto_yt_client_discovery_client_proto_discovery_client_service_proto_rawDescGZIP(), []int{2}
}

func (x *TListMembersOptions) GetLimit() int32 {
	if x != nil && x.Limit != nil {
		return *x.Limit
	}
	return 0
}

func (x *TListMembersOptions) GetAttributeKeys() []string {
	if x != nil {
		return x.AttributeKeys
	}
	return nil
}

type TListGroupsOptions struct {
	state         protoimpl.MessageState
	sizeCache     protoimpl.SizeCache
	unknownFields protoimpl.UnknownFields

	Limit *int32 `protobuf:"varint,1,req,name=limit" json:"limit,omitempty"`
}

func (x *TListGroupsOptions) Reset() {
	*x = TListGroupsOptions{}
	if protoimpl.UnsafeEnabled {
		mi := &file_yt_yt_proto_yt_client_discovery_client_proto_discovery_client_service_proto_msgTypes[3]
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		ms.StoreMessageInfo(mi)
	}
}

func (x *TListGroupsOptions) String() string {
	return protoimpl.X.MessageStringOf(x)
}

func (*TListGroupsOptions) ProtoMessage() {}

func (x *TListGroupsOptions) ProtoReflect() protoreflect.Message {
	mi := &file_yt_yt_proto_yt_client_discovery_client_proto_discovery_client_service_proto_msgTypes[3]
	if protoimpl.UnsafeEnabled && x != nil {
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		if ms.LoadMessageInfo() == nil {
			ms.StoreMessageInfo(mi)
		}
		return ms
	}
	return mi.MessageOf(x)
}

// Deprecated: Use TListGroupsOptions.ProtoReflect.Descriptor instead.
func (*TListGroupsOptions) Descriptor() ([]byte, []int) {
	return file_yt_yt_proto_yt_client_discovery_client_proto_discovery_client_service_proto_rawDescGZIP(), []int{3}
}

func (x *TListGroupsOptions) GetLimit() int32 {
	if x != nil && x.Limit != nil {
		return *x.Limit
	}
	return 0
}

type TReqListMembers struct {
	state         protoimpl.MessageState
	sizeCache     protoimpl.SizeCache
	unknownFields protoimpl.UnknownFields

	GroupId *string              `protobuf:"bytes,1,req,name=group_id,json=groupId" json:"group_id,omitempty"`
	Options *TListMembersOptions `protobuf:"bytes,2,req,name=options" json:"options,omitempty"`
}

func (x *TReqListMembers) Reset() {
	*x = TReqListMembers{}
	if protoimpl.UnsafeEnabled {
		mi := &file_yt_yt_proto_yt_client_discovery_client_proto_discovery_client_service_proto_msgTypes[4]
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		ms.StoreMessageInfo(mi)
	}
}

func (x *TReqListMembers) String() string {
	return protoimpl.X.MessageStringOf(x)
}

func (*TReqListMembers) ProtoMessage() {}

func (x *TReqListMembers) ProtoReflect() protoreflect.Message {
	mi := &file_yt_yt_proto_yt_client_discovery_client_proto_discovery_client_service_proto_msgTypes[4]
	if protoimpl.UnsafeEnabled && x != nil {
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		if ms.LoadMessageInfo() == nil {
			ms.StoreMessageInfo(mi)
		}
		return ms
	}
	return mi.MessageOf(x)
}

// Deprecated: Use TReqListMembers.ProtoReflect.Descriptor instead.
func (*TReqListMembers) Descriptor() ([]byte, []int) {
	return file_yt_yt_proto_yt_client_discovery_client_proto_discovery_client_service_proto_rawDescGZIP(), []int{4}
}

func (x *TReqListMembers) GetGroupId() string {
	if x != nil && x.GroupId != nil {
		return *x.GroupId
	}
	return ""
}

func (x *TReqListMembers) GetOptions() *TListMembersOptions {
	if x != nil {
		return x.Options
	}
	return nil
}

type TRspListMembers struct {
	state         protoimpl.MessageState
	sizeCache     protoimpl.SizeCache
	unknownFields protoimpl.UnknownFields

	Members []*TMemberInfo `protobuf:"bytes,1,rep,name=members" json:"members,omitempty"`
}

func (x *TRspListMembers) Reset() {
	*x = TRspListMembers{}
	if protoimpl.UnsafeEnabled {
		mi := &file_yt_yt_proto_yt_client_discovery_client_proto_discovery_client_service_proto_msgTypes[5]
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		ms.StoreMessageInfo(mi)
	}
}

func (x *TRspListMembers) String() string {
	return protoimpl.X.MessageStringOf(x)
}

func (*TRspListMembers) ProtoMessage() {}

func (x *TRspListMembers) ProtoReflect() protoreflect.Message {
	mi := &file_yt_yt_proto_yt_client_discovery_client_proto_discovery_client_service_proto_msgTypes[5]
	if protoimpl.UnsafeEnabled && x != nil {
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		if ms.LoadMessageInfo() == nil {
			ms.StoreMessageInfo(mi)
		}
		return ms
	}
	return mi.MessageOf(x)
}

// Deprecated: Use TRspListMembers.ProtoReflect.Descriptor instead.
func (*TRspListMembers) Descriptor() ([]byte, []int) {
	return file_yt_yt_proto_yt_client_discovery_client_proto_discovery_client_service_proto_rawDescGZIP(), []int{5}
}

func (x *TRspListMembers) GetMembers() []*TMemberInfo {
	if x != nil {
		return x.Members
	}
	return nil
}

type TReqListGroups struct {
	state         protoimpl.MessageState
	sizeCache     protoimpl.SizeCache
	unknownFields protoimpl.UnknownFields

	Prefix  *string             `protobuf:"bytes,1,req,name=prefix" json:"prefix,omitempty"`
	Options *TListGroupsOptions `protobuf:"bytes,2,req,name=options" json:"options,omitempty"`
}

func (x *TReqListGroups) Reset() {
	*x = TReqListGroups{}
	if protoimpl.UnsafeEnabled {
		mi := &file_yt_yt_proto_yt_client_discovery_client_proto_discovery_client_service_proto_msgTypes[6]
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		ms.StoreMessageInfo(mi)
	}
}

func (x *TReqListGroups) String() string {
	return protoimpl.X.MessageStringOf(x)
}

func (*TReqListGroups) ProtoMessage() {}

func (x *TReqListGroups) ProtoReflect() protoreflect.Message {
	mi := &file_yt_yt_proto_yt_client_discovery_client_proto_discovery_client_service_proto_msgTypes[6]
	if protoimpl.UnsafeEnabled && x != nil {
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		if ms.LoadMessageInfo() == nil {
			ms.StoreMessageInfo(mi)
		}
		return ms
	}
	return mi.MessageOf(x)
}

// Deprecated: Use TReqListGroups.ProtoReflect.Descriptor instead.
func (*TReqListGroups) Descriptor() ([]byte, []int) {
	return file_yt_yt_proto_yt_client_discovery_client_proto_discovery_client_service_proto_rawDescGZIP(), []int{6}
}

func (x *TReqListGroups) GetPrefix() string {
	if x != nil && x.Prefix != nil {
		return *x.Prefix
	}
	return ""
}

func (x *TReqListGroups) GetOptions() *TListGroupsOptions {
	if x != nil {
		return x.Options
	}
	return nil
}

type TRspListGroups struct {
	state         protoimpl.MessageState
	sizeCache     protoimpl.SizeCache
	unknownFields protoimpl.UnknownFields

	GroupIds   []string `protobuf:"bytes,1,rep,name=group_ids,json=groupIds" json:"group_ids,omitempty"`
	Incomplete *bool    `protobuf:"varint,2,req,name=incomplete" json:"incomplete,omitempty"`
}

func (x *TRspListGroups) Reset() {
	*x = TRspListGroups{}
	if protoimpl.UnsafeEnabled {
		mi := &file_yt_yt_proto_yt_client_discovery_client_proto_discovery_client_service_proto_msgTypes[7]
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		ms.StoreMessageInfo(mi)
	}
}

func (x *TRspListGroups) String() string {
	return protoimpl.X.MessageStringOf(x)
}

func (*TRspListGroups) ProtoMessage() {}

func (x *TRspListGroups) ProtoReflect() protoreflect.Message {
	mi := &file_yt_yt_proto_yt_client_discovery_client_proto_discovery_client_service_proto_msgTypes[7]
	if protoimpl.UnsafeEnabled && x != nil {
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		if ms.LoadMessageInfo() == nil {
			ms.StoreMessageInfo(mi)
		}
		return ms
	}
	return mi.MessageOf(x)
}

// Deprecated: Use TRspListGroups.ProtoReflect.Descriptor instead.
func (*TRspListGroups) Descriptor() ([]byte, []int) {
	return file_yt_yt_proto_yt_client_discovery_client_proto_discovery_client_service_proto_rawDescGZIP(), []int{7}
}

func (x *TRspListGroups) GetGroupIds() []string {
	if x != nil {
		return x.GroupIds
	}
	return nil
}

func (x *TRspListGroups) GetIncomplete() bool {
	if x != nil && x.Incomplete != nil {
		return *x.Incomplete
	}
	return false
}

type TReqGetGroupMeta struct {
	state         protoimpl.MessageState
	sizeCache     protoimpl.SizeCache
	unknownFields protoimpl.UnknownFields

	GroupId *string `protobuf:"bytes,1,req,name=group_id,json=groupId" json:"group_id,omitempty"`
}

func (x *TReqGetGroupMeta) Reset() {
	*x = TReqGetGroupMeta{}
	if protoimpl.UnsafeEnabled {
		mi := &file_yt_yt_proto_yt_client_discovery_client_proto_discovery_client_service_proto_msgTypes[8]
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		ms.StoreMessageInfo(mi)
	}
}

func (x *TReqGetGroupMeta) String() string {
	return protoimpl.X.MessageStringOf(x)
}

func (*TReqGetGroupMeta) ProtoMessage() {}

func (x *TReqGetGroupMeta) ProtoReflect() protoreflect.Message {
	mi := &file_yt_yt_proto_yt_client_discovery_client_proto_discovery_client_service_proto_msgTypes[8]
	if protoimpl.UnsafeEnabled && x != nil {
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		if ms.LoadMessageInfo() == nil {
			ms.StoreMessageInfo(mi)
		}
		return ms
	}
	return mi.MessageOf(x)
}

// Deprecated: Use TReqGetGroupMeta.ProtoReflect.Descriptor instead.
func (*TReqGetGroupMeta) Descriptor() ([]byte, []int) {
	return file_yt_yt_proto_yt_client_discovery_client_proto_discovery_client_service_proto_rawDescGZIP(), []int{8}
}

func (x *TReqGetGroupMeta) GetGroupId() string {
	if x != nil && x.GroupId != nil {
		return *x.GroupId
	}
	return ""
}

type TRspGetGroupMeta struct {
	state         protoimpl.MessageState
	sizeCache     protoimpl.SizeCache
	unknownFields protoimpl.UnknownFields

	Meta *TGroupMeta `protobuf:"bytes,1,req,name=meta" json:"meta,omitempty"`
}

func (x *TRspGetGroupMeta) Reset() {
	*x = TRspGetGroupMeta{}
	if protoimpl.UnsafeEnabled {
		mi := &file_yt_yt_proto_yt_client_discovery_client_proto_discovery_client_service_proto_msgTypes[9]
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		ms.StoreMessageInfo(mi)
	}
}

func (x *TRspGetGroupMeta) String() string {
	return protoimpl.X.MessageStringOf(x)
}

func (*TRspGetGroupMeta) ProtoMessage() {}

func (x *TRspGetGroupMeta) ProtoReflect() protoreflect.Message {
	mi := &file_yt_yt_proto_yt_client_discovery_client_proto_discovery_client_service_proto_msgTypes[9]
	if protoimpl.UnsafeEnabled && x != nil {
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		if ms.LoadMessageInfo() == nil {
			ms.StoreMessageInfo(mi)
		}
		return ms
	}
	return mi.MessageOf(x)
}

// Deprecated: Use TRspGetGroupMeta.ProtoReflect.Descriptor instead.
func (*TRspGetGroupMeta) Descriptor() ([]byte, []int) {
	return file_yt_yt_proto_yt_client_discovery_client_proto_discovery_client_service_proto_rawDescGZIP(), []int{9}
}

func (x *TRspGetGroupMeta) GetMeta() *TGroupMeta {
	if x != nil {
		return x.Meta
	}
	return nil
}

type TReqHeartbeat struct {
	state         protoimpl.MessageState
	sizeCache     protoimpl.SizeCache
	unknownFields protoimpl.UnknownFields

	GroupId      *string      `protobuf:"bytes,1,req,name=group_id,json=groupId" json:"group_id,omitempty"`
	MemberInfo   *TMemberInfo `protobuf:"bytes,2,req,name=member_info,json=memberInfo" json:"member_info,omitempty"`
	LeaseTimeout *int64       `protobuf:"varint,3,req,name=lease_timeout,json=leaseTimeout" json:"lease_timeout,omitempty"`
}

func (x *TReqHeartbeat) Reset() {
	*x = TReqHeartbeat{}
	if protoimpl.UnsafeEnabled {
		mi := &file_yt_yt_proto_yt_client_discovery_client_proto_discovery_client_service_proto_msgTypes[10]
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		ms.StoreMessageInfo(mi)
	}
}

func (x *TReqHeartbeat) String() string {
	return protoimpl.X.MessageStringOf(x)
}

func (*TReqHeartbeat) ProtoMessage() {}

func (x *TReqHeartbeat) ProtoReflect() protoreflect.Message {
	mi := &file_yt_yt_proto_yt_client_discovery_client_proto_discovery_client_service_proto_msgTypes[10]
	if protoimpl.UnsafeEnabled && x != nil {
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		if ms.LoadMessageInfo() == nil {
			ms.StoreMessageInfo(mi)
		}
		return ms
	}
	return mi.MessageOf(x)
}

// Deprecated: Use TReqHeartbeat.ProtoReflect.Descriptor instead.
func (*TReqHeartbeat) Descriptor() ([]byte, []int) {
	return file_yt_yt_proto_yt_client_discovery_client_proto_discovery_client_service_proto_rawDescGZIP(), []int{10}
}

func (x *TReqHeartbeat) GetGroupId() string {
	if x != nil && x.GroupId != nil {
		return *x.GroupId
	}
	return ""
}

func (x *TReqHeartbeat) GetMemberInfo() *TMemberInfo {
	if x != nil {
		return x.MemberInfo
	}
	return nil
}

func (x *TReqHeartbeat) GetLeaseTimeout() int64 {
	if x != nil && x.LeaseTimeout != nil {
		return *x.LeaseTimeout
	}
	return 0
}

type TRspHeartbeat struct {
	state         protoimpl.MessageState
	sizeCache     protoimpl.SizeCache
	unknownFields protoimpl.UnknownFields
}

func (x *TRspHeartbeat) Reset() {
	*x = TRspHeartbeat{}
	if protoimpl.UnsafeEnabled {
		mi := &file_yt_yt_proto_yt_client_discovery_client_proto_discovery_client_service_proto_msgTypes[11]
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		ms.StoreMessageInfo(mi)
	}
}

func (x *TRspHeartbeat) String() string {
	return protoimpl.X.MessageStringOf(x)
}

func (*TRspHeartbeat) ProtoMessage() {}

func (x *TRspHeartbeat) ProtoReflect() protoreflect.Message {
	mi := &file_yt_yt_proto_yt_client_discovery_client_proto_discovery_client_service_proto_msgTypes[11]
	if protoimpl.UnsafeEnabled && x != nil {
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		if ms.LoadMessageInfo() == nil {
			ms.StoreMessageInfo(mi)
		}
		return ms
	}
	return mi.MessageOf(x)
}

// Deprecated: Use TRspHeartbeat.ProtoReflect.Descriptor instead.
func (*TRspHeartbeat) Descriptor() ([]byte, []int) {
	return file_yt_yt_proto_yt_client_discovery_client_proto_discovery_client_service_proto_rawDescGZIP(), []int{11}
}

var File_yt_yt_proto_yt_client_discovery_client_proto_discovery_client_service_proto protoreflect.FileDescriptor

var file_yt_yt_proto_yt_client_discovery_client_proto_discovery_client_service_proto_rawDesc = []byte{
	0x0a, 0x4b, 0x79, 0x74, 0x2f, 0x79, 0x74, 0x5f, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x2f, 0x79, 0x74,
	0x2f, 0x63, 0x6c, 0x69, 0x65, 0x6e, 0x74, 0x2f, 0x64, 0x69, 0x73, 0x63, 0x6f, 0x76, 0x65, 0x72,
	0x79, 0x5f, 0x63, 0x6c, 0x69, 0x65, 0x6e, 0x74, 0x2f, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x2f, 0x64,
	0x69, 0x73, 0x63, 0x6f, 0x76, 0x65, 0x72, 0x79, 0x5f, 0x63, 0x6c, 0x69, 0x65, 0x6e, 0x74, 0x5f,
	0x73, 0x65, 0x72, 0x76, 0x69, 0x63, 0x65, 0x2e, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x12, 0x1b, 0x4e,
	0x59, 0x54, 0x2e, 0x4e, 0x44, 0x69, 0x73, 0x63, 0x6f, 0x76, 0x65, 0x72, 0x79, 0x43, 0x6c, 0x69,
	0x65, 0x6e, 0x74, 0x2e, 0x4e, 0x50, 0x72, 0x6f, 0x74, 0x6f, 0x1a, 0x2d, 0x79, 0x74, 0x5f, 0x70,
	0x72, 0x6f, 0x74, 0x6f, 0x2f, 0x79, 0x74, 0x2f, 0x63, 0x6f, 0x72, 0x65, 0x2f, 0x79, 0x74, 0x72,
	0x65, 0x65, 0x2f, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x2f, 0x61, 0x74, 0x74, 0x72, 0x69, 0x62, 0x75,
	0x74, 0x65, 0x73, 0x2e, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x22, 0x9e, 0x01, 0x0a, 0x0b, 0x54, 0x4d,
	0x65, 0x6d, 0x62, 0x65, 0x72, 0x49, 0x6e, 0x66, 0x6f, 0x12, 0x0e, 0x0a, 0x02, 0x69, 0x64, 0x18,
	0x01, 0x20, 0x02, 0x28, 0x09, 0x52, 0x02, 0x69, 0x64, 0x12, 0x1a, 0x0a, 0x08, 0x70, 0x72, 0x69,
	0x6f, 0x72, 0x69, 0x74, 0x79, 0x18, 0x02, 0x20, 0x02, 0x28, 0x03, 0x52, 0x08, 0x70, 0x72, 0x69,
	0x6f, 0x72, 0x69, 0x74, 0x79, 0x12, 0x1a, 0x0a, 0x08, 0x72, 0x65, 0x76, 0x69, 0x73, 0x69, 0x6f,
	0x6e, 0x18, 0x03, 0x20, 0x02, 0x28, 0x03, 0x52, 0x08, 0x72, 0x65, 0x76, 0x69, 0x73, 0x69, 0x6f,
	0x6e, 0x12, 0x47, 0x0a, 0x0a, 0x61, 0x74, 0x74, 0x72, 0x69, 0x62, 0x75, 0x74, 0x65, 0x73, 0x18,
	0x04, 0x20, 0x01, 0x28, 0x0b, 0x32, 0x27, 0x2e, 0x4e, 0x59, 0x54, 0x2e, 0x4e, 0x59, 0x54, 0x72,
	0x65, 0x65, 0x2e, 0x4e, 0x50, 0x72, 0x6f, 0x74, 0x6f, 0x2e, 0x54, 0x41, 0x74, 0x74, 0x72, 0x69,
	0x62, 0x75, 0x74, 0x65, 0x44, 0x69, 0x63, 0x74, 0x69, 0x6f, 0x6e, 0x61, 0x72, 0x79, 0x52, 0x0a,
	0x61, 0x74, 0x74, 0x72, 0x69, 0x62, 0x75, 0x74, 0x65, 0x73, 0x22, 0x2f, 0x0a, 0x0a, 0x54, 0x47,
	0x72, 0x6f, 0x75, 0x70, 0x4d, 0x65, 0x74, 0x61, 0x12, 0x21, 0x0a, 0x0c, 0x6d, 0x65, 0x6d, 0x62,
	0x65, 0x72, 0x5f, 0x63, 0x6f, 0x75, 0x6e, 0x74, 0x18, 0x01, 0x20, 0x02, 0x28, 0x05, 0x52, 0x0b,
	0x6d, 0x65, 0x6d, 0x62, 0x65, 0x72, 0x43, 0x6f, 0x75, 0x6e, 0x74, 0x22, 0x52, 0x0a, 0x13, 0x54,
	0x4c, 0x69, 0x73, 0x74, 0x4d, 0x65, 0x6d, 0x62, 0x65, 0x72, 0x73, 0x4f, 0x70, 0x74, 0x69, 0x6f,
	0x6e, 0x73, 0x12, 0x14, 0x0a, 0x05, 0x6c, 0x69, 0x6d, 0x69, 0x74, 0x18, 0x01, 0x20, 0x02, 0x28,
	0x05, 0x52, 0x05, 0x6c, 0x69, 0x6d, 0x69, 0x74, 0x12, 0x25, 0x0a, 0x0e, 0x61, 0x74, 0x74, 0x72,
	0x69, 0x62, 0x75, 0x74, 0x65, 0x5f, 0x6b, 0x65, 0x79, 0x73, 0x18, 0x02, 0x20, 0x03, 0x28, 0x09,
	0x52, 0x0d, 0x61, 0x74, 0x74, 0x72, 0x69, 0x62, 0x75, 0x74, 0x65, 0x4b, 0x65, 0x79, 0x73, 0x22,
	0x2a, 0x0a, 0x12, 0x54, 0x4c, 0x69, 0x73, 0x74, 0x47, 0x72, 0x6f, 0x75, 0x70, 0x73, 0x4f, 0x70,
	0x74, 0x69, 0x6f, 0x6e, 0x73, 0x12, 0x14, 0x0a, 0x05, 0x6c, 0x69, 0x6d, 0x69, 0x74, 0x18, 0x01,
	0x20, 0x02, 0x28, 0x05, 0x52, 0x05, 0x6c, 0x69, 0x6d, 0x69, 0x74, 0x22, 0x78, 0x0a, 0x0f, 0x54,
	0x52, 0x65, 0x71, 0x4c, 0x69, 0x73, 0x74, 0x4d, 0x65, 0x6d, 0x62, 0x65, 0x72, 0x73, 0x12, 0x19,
	0x0a, 0x08, 0x67, 0x72, 0x6f, 0x75, 0x70, 0x5f, 0x69, 0x64, 0x18, 0x01, 0x20, 0x02, 0x28, 0x09,
	0x52, 0x07, 0x67, 0x72, 0x6f, 0x75, 0x70, 0x49, 0x64, 0x12, 0x4a, 0x0a, 0x07, 0x6f, 0x70, 0x74,
	0x69, 0x6f, 0x6e, 0x73, 0x18, 0x02, 0x20, 0x02, 0x28, 0x0b, 0x32, 0x30, 0x2e, 0x4e, 0x59, 0x54,
	0x2e, 0x4e, 0x44, 0x69, 0x73, 0x63, 0x6f, 0x76, 0x65, 0x72, 0x79, 0x43, 0x6c, 0x69, 0x65, 0x6e,
	0x74, 0x2e, 0x4e, 0x50, 0x72, 0x6f, 0x74, 0x6f, 0x2e, 0x54, 0x4c, 0x69, 0x73, 0x74, 0x4d, 0x65,
	0x6d, 0x62, 0x65, 0x72, 0x73, 0x4f, 0x70, 0x74, 0x69, 0x6f, 0x6e, 0x73, 0x52, 0x07, 0x6f, 0x70,
	0x74, 0x69, 0x6f, 0x6e, 0x73, 0x22, 0x55, 0x0a, 0x0f, 0x54, 0x52, 0x73, 0x70, 0x4c, 0x69, 0x73,
	0x74, 0x4d, 0x65, 0x6d, 0x62, 0x65, 0x72, 0x73, 0x12, 0x42, 0x0a, 0x07, 0x6d, 0x65, 0x6d, 0x62,
	0x65, 0x72, 0x73, 0x18, 0x01, 0x20, 0x03, 0x28, 0x0b, 0x32, 0x28, 0x2e, 0x4e, 0x59, 0x54, 0x2e,
	0x4e, 0x44, 0x69, 0x73, 0x63, 0x6f, 0x76, 0x65, 0x72, 0x79, 0x43, 0x6c, 0x69, 0x65, 0x6e, 0x74,
	0x2e, 0x4e, 0x50, 0x72, 0x6f, 0x74, 0x6f, 0x2e, 0x54, 0x4d, 0x65, 0x6d, 0x62, 0x65, 0x72, 0x49,
	0x6e, 0x66, 0x6f, 0x52, 0x07, 0x6d, 0x65, 0x6d, 0x62, 0x65, 0x72, 0x73, 0x22, 0x73, 0x0a, 0x0e,
	0x54, 0x52, 0x65, 0x71, 0x4c, 0x69, 0x73, 0x74, 0x47, 0x72, 0x6f, 0x75, 0x70, 0x73, 0x12, 0x16,
	0x0a, 0x06, 0x70, 0x72, 0x65, 0x66, 0x69, 0x78, 0x18, 0x01, 0x20, 0x02, 0x28, 0x09, 0x52, 0x06,
	0x70, 0x72, 0x65, 0x66, 0x69, 0x78, 0x12, 0x49, 0x0a, 0x07, 0x6f, 0x70, 0x74, 0x69, 0x6f, 0x6e,
	0x73, 0x18, 0x02, 0x20, 0x02, 0x28, 0x0b, 0x32, 0x2f, 0x2e, 0x4e, 0x59, 0x54, 0x2e, 0x4e, 0x44,
	0x69, 0x73, 0x63, 0x6f, 0x76, 0x65, 0x72, 0x79, 0x43, 0x6c, 0x69, 0x65, 0x6e, 0x74, 0x2e, 0x4e,
	0x50, 0x72, 0x6f, 0x74, 0x6f, 0x2e, 0x54, 0x4c, 0x69, 0x73, 0x74, 0x47, 0x72, 0x6f, 0x75, 0x70,
	0x73, 0x4f, 0x70, 0x74, 0x69, 0x6f, 0x6e, 0x73, 0x52, 0x07, 0x6f, 0x70, 0x74, 0x69, 0x6f, 0x6e,
	0x73, 0x22, 0x4d, 0x0a, 0x0e, 0x54, 0x52, 0x73, 0x70, 0x4c, 0x69, 0x73, 0x74, 0x47, 0x72, 0x6f,
	0x75, 0x70, 0x73, 0x12, 0x1b, 0x0a, 0x09, 0x67, 0x72, 0x6f, 0x75, 0x70, 0x5f, 0x69, 0x64, 0x73,
	0x18, 0x01, 0x20, 0x03, 0x28, 0x09, 0x52, 0x08, 0x67, 0x72, 0x6f, 0x75, 0x70, 0x49, 0x64, 0x73,
	0x12, 0x1e, 0x0a, 0x0a, 0x69, 0x6e, 0x63, 0x6f, 0x6d, 0x70, 0x6c, 0x65, 0x74, 0x65, 0x18, 0x02,
	0x20, 0x02, 0x28, 0x08, 0x52, 0x0a, 0x69, 0x6e, 0x63, 0x6f, 0x6d, 0x70, 0x6c, 0x65, 0x74, 0x65,
	0x22, 0x2d, 0x0a, 0x10, 0x54, 0x52, 0x65, 0x71, 0x47, 0x65, 0x74, 0x47, 0x72, 0x6f, 0x75, 0x70,
	0x4d, 0x65, 0x74, 0x61, 0x12, 0x19, 0x0a, 0x08, 0x67, 0x72, 0x6f, 0x75, 0x70, 0x5f, 0x69, 0x64,
	0x18, 0x01, 0x20, 0x02, 0x28, 0x09, 0x52, 0x07, 0x67, 0x72, 0x6f, 0x75, 0x70, 0x49, 0x64, 0x22,
	0x4f, 0x0a, 0x10, 0x54, 0x52, 0x73, 0x70, 0x47, 0x65, 0x74, 0x47, 0x72, 0x6f, 0x75, 0x70, 0x4d,
	0x65, 0x74, 0x61, 0x12, 0x3b, 0x0a, 0x04, 0x6d, 0x65, 0x74, 0x61, 0x18, 0x01, 0x20, 0x02, 0x28,
	0x0b, 0x32, 0x27, 0x2e, 0x4e, 0x59, 0x54, 0x2e, 0x4e, 0x44, 0x69, 0x73, 0x63, 0x6f, 0x76, 0x65,
	0x72, 0x79, 0x43, 0x6c, 0x69, 0x65, 0x6e, 0x74, 0x2e, 0x4e, 0x50, 0x72, 0x6f, 0x74, 0x6f, 0x2e,
	0x54, 0x47, 0x72, 0x6f, 0x75, 0x70, 0x4d, 0x65, 0x74, 0x61, 0x52, 0x04, 0x6d, 0x65, 0x74, 0x61,
	0x22, 0x9a, 0x01, 0x0a, 0x0d, 0x54, 0x52, 0x65, 0x71, 0x48, 0x65, 0x61, 0x72, 0x74, 0x62, 0x65,
	0x61, 0x74, 0x12, 0x19, 0x0a, 0x08, 0x67, 0x72, 0x6f, 0x75, 0x70, 0x5f, 0x69, 0x64, 0x18, 0x01,
	0x20, 0x02, 0x28, 0x09, 0x52, 0x07, 0x67, 0x72, 0x6f, 0x75, 0x70, 0x49, 0x64, 0x12, 0x49, 0x0a,
	0x0b, 0x6d, 0x65, 0x6d, 0x62, 0x65, 0x72, 0x5f, 0x69, 0x6e, 0x66, 0x6f, 0x18, 0x02, 0x20, 0x02,
	0x28, 0x0b, 0x32, 0x28, 0x2e, 0x4e, 0x59, 0x54, 0x2e, 0x4e, 0x44, 0x69, 0x73, 0x63, 0x6f, 0x76,
	0x65, 0x72, 0x79, 0x43, 0x6c, 0x69, 0x65, 0x6e, 0x74, 0x2e, 0x4e, 0x50, 0x72, 0x6f, 0x74, 0x6f,
	0x2e, 0x54, 0x4d, 0x65, 0x6d, 0x62, 0x65, 0x72, 0x49, 0x6e, 0x66, 0x6f, 0x52, 0x0a, 0x6d, 0x65,
	0x6d, 0x62, 0x65, 0x72, 0x49, 0x6e, 0x66, 0x6f, 0x12, 0x23, 0x0a, 0x0d, 0x6c, 0x65, 0x61, 0x73,
	0x65, 0x5f, 0x74, 0x69, 0x6d, 0x65, 0x6f, 0x75, 0x74, 0x18, 0x03, 0x20, 0x02, 0x28, 0x03, 0x52,
	0x0c, 0x6c, 0x65, 0x61, 0x73, 0x65, 0x54, 0x69, 0x6d, 0x65, 0x6f, 0x75, 0x74, 0x22, 0x0f, 0x0a,
	0x0d, 0x54, 0x52, 0x73, 0x70, 0x48, 0x65, 0x61, 0x72, 0x74, 0x62, 0x65, 0x61, 0x74, 0x42, 0x5e,
	0x0a, 0x0d, 0x74, 0x65, 0x63, 0x68, 0x2e, 0x79, 0x74, 0x73, 0x61, 0x75, 0x72, 0x75, 0x73, 0x42,
	0x15, 0x44, 0x69, 0x73, 0x63, 0x6f, 0x76, 0x65, 0x72, 0x79, 0x43, 0x6c, 0x69, 0x65, 0x6e, 0x74,
	0x50, 0x72, 0x6f, 0x74, 0x6f, 0x73, 0x50, 0x01, 0x5a, 0x34, 0x61, 0x2e, 0x79, 0x61, 0x6e, 0x64,
	0x65, 0x78, 0x2d, 0x74, 0x65, 0x61, 0x6d, 0x2e, 0x72, 0x75, 0x2f, 0x79, 0x74, 0x2f, 0x67, 0x6f,
	0x2f, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x2f, 0x63, 0x6c, 0x69, 0x65, 0x6e, 0x74, 0x2f, 0x64, 0x69,
	0x73, 0x63, 0x6f, 0x76, 0x65, 0x72, 0x79, 0x5f, 0x63, 0x6c, 0x69, 0x65, 0x6e, 0x74,
}

var (
	file_yt_yt_proto_yt_client_discovery_client_proto_discovery_client_service_proto_rawDescOnce sync.Once
	file_yt_yt_proto_yt_client_discovery_client_proto_discovery_client_service_proto_rawDescData = file_yt_yt_proto_yt_client_discovery_client_proto_discovery_client_service_proto_rawDesc
)

func file_yt_yt_proto_yt_client_discovery_client_proto_discovery_client_service_proto_rawDescGZIP() []byte {
	file_yt_yt_proto_yt_client_discovery_client_proto_discovery_client_service_proto_rawDescOnce.Do(func() {
		file_yt_yt_proto_yt_client_discovery_client_proto_discovery_client_service_proto_rawDescData = protoimpl.X.CompressGZIP(file_yt_yt_proto_yt_client_discovery_client_proto_discovery_client_service_proto_rawDescData)
	})
	return file_yt_yt_proto_yt_client_discovery_client_proto_discovery_client_service_proto_rawDescData
}

var file_yt_yt_proto_yt_client_discovery_client_proto_discovery_client_service_proto_msgTypes = make([]protoimpl.MessageInfo, 12)
var file_yt_yt_proto_yt_client_discovery_client_proto_discovery_client_service_proto_goTypes = []interface{}{
	(*TMemberInfo)(nil),                // 0: NYT.NDiscoveryClient.NProto.TMemberInfo
	(*TGroupMeta)(nil),                 // 1: NYT.NDiscoveryClient.NProto.TGroupMeta
	(*TListMembersOptions)(nil),        // 2: NYT.NDiscoveryClient.NProto.TListMembersOptions
	(*TListGroupsOptions)(nil),         // 3: NYT.NDiscoveryClient.NProto.TListGroupsOptions
	(*TReqListMembers)(nil),            // 4: NYT.NDiscoveryClient.NProto.TReqListMembers
	(*TRspListMembers)(nil),            // 5: NYT.NDiscoveryClient.NProto.TRspListMembers
	(*TReqListGroups)(nil),             // 6: NYT.NDiscoveryClient.NProto.TReqListGroups
	(*TRspListGroups)(nil),             // 7: NYT.NDiscoveryClient.NProto.TRspListGroups
	(*TReqGetGroupMeta)(nil),           // 8: NYT.NDiscoveryClient.NProto.TReqGetGroupMeta
	(*TRspGetGroupMeta)(nil),           // 9: NYT.NDiscoveryClient.NProto.TRspGetGroupMeta
	(*TReqHeartbeat)(nil),              // 10: NYT.NDiscoveryClient.NProto.TReqHeartbeat
	(*TRspHeartbeat)(nil),              // 11: NYT.NDiscoveryClient.NProto.TRspHeartbeat
	(*ytree.TAttributeDictionary)(nil), // 12: NYT.NYTree.NProto.TAttributeDictionary
}
var file_yt_yt_proto_yt_client_discovery_client_proto_discovery_client_service_proto_depIdxs = []int32{
	12, // 0: NYT.NDiscoveryClient.NProto.TMemberInfo.attributes:type_name -> NYT.NYTree.NProto.TAttributeDictionary
	2,  // 1: NYT.NDiscoveryClient.NProto.TReqListMembers.options:type_name -> NYT.NDiscoveryClient.NProto.TListMembersOptions
	0,  // 2: NYT.NDiscoveryClient.NProto.TRspListMembers.members:type_name -> NYT.NDiscoveryClient.NProto.TMemberInfo
	3,  // 3: NYT.NDiscoveryClient.NProto.TReqListGroups.options:type_name -> NYT.NDiscoveryClient.NProto.TListGroupsOptions
	1,  // 4: NYT.NDiscoveryClient.NProto.TRspGetGroupMeta.meta:type_name -> NYT.NDiscoveryClient.NProto.TGroupMeta
	0,  // 5: NYT.NDiscoveryClient.NProto.TReqHeartbeat.member_info:type_name -> NYT.NDiscoveryClient.NProto.TMemberInfo
	6,  // [6:6] is the sub-list for method output_type
	6,  // [6:6] is the sub-list for method input_type
	6,  // [6:6] is the sub-list for extension type_name
	6,  // [6:6] is the sub-list for extension extendee
	0,  // [0:6] is the sub-list for field type_name
}

func init() { file_yt_yt_proto_yt_client_discovery_client_proto_discovery_client_service_proto_init() }
func file_yt_yt_proto_yt_client_discovery_client_proto_discovery_client_service_proto_init() {
	if File_yt_yt_proto_yt_client_discovery_client_proto_discovery_client_service_proto != nil {
		return
	}
	if !protoimpl.UnsafeEnabled {
		file_yt_yt_proto_yt_client_discovery_client_proto_discovery_client_service_proto_msgTypes[0].Exporter = func(v interface{}, i int) interface{} {
			switch v := v.(*TMemberInfo); i {
			case 0:
				return &v.state
			case 1:
				return &v.sizeCache
			case 2:
				return &v.unknownFields
			default:
				return nil
			}
		}
		file_yt_yt_proto_yt_client_discovery_client_proto_discovery_client_service_proto_msgTypes[1].Exporter = func(v interface{}, i int) interface{} {
			switch v := v.(*TGroupMeta); i {
			case 0:
				return &v.state
			case 1:
				return &v.sizeCache
			case 2:
				return &v.unknownFields
			default:
				return nil
			}
		}
		file_yt_yt_proto_yt_client_discovery_client_proto_discovery_client_service_proto_msgTypes[2].Exporter = func(v interface{}, i int) interface{} {
			switch v := v.(*TListMembersOptions); i {
			case 0:
				return &v.state
			case 1:
				return &v.sizeCache
			case 2:
				return &v.unknownFields
			default:
				return nil
			}
		}
		file_yt_yt_proto_yt_client_discovery_client_proto_discovery_client_service_proto_msgTypes[3].Exporter = func(v interface{}, i int) interface{} {
			switch v := v.(*TListGroupsOptions); i {
			case 0:
				return &v.state
			case 1:
				return &v.sizeCache
			case 2:
				return &v.unknownFields
			default:
				return nil
			}
		}
		file_yt_yt_proto_yt_client_discovery_client_proto_discovery_client_service_proto_msgTypes[4].Exporter = func(v interface{}, i int) interface{} {
			switch v := v.(*TReqListMembers); i {
			case 0:
				return &v.state
			case 1:
				return &v.sizeCache
			case 2:
				return &v.unknownFields
			default:
				return nil
			}
		}
		file_yt_yt_proto_yt_client_discovery_client_proto_discovery_client_service_proto_msgTypes[5].Exporter = func(v interface{}, i int) interface{} {
			switch v := v.(*TRspListMembers); i {
			case 0:
				return &v.state
			case 1:
				return &v.sizeCache
			case 2:
				return &v.unknownFields
			default:
				return nil
			}
		}
		file_yt_yt_proto_yt_client_discovery_client_proto_discovery_client_service_proto_msgTypes[6].Exporter = func(v interface{}, i int) interface{} {
			switch v := v.(*TReqListGroups); i {
			case 0:
				return &v.state
			case 1:
				return &v.sizeCache
			case 2:
				return &v.unknownFields
			default:
				return nil
			}
		}
		file_yt_yt_proto_yt_client_discovery_client_proto_discovery_client_service_proto_msgTypes[7].Exporter = func(v interface{}, i int) interface{} {
			switch v := v.(*TRspListGroups); i {
			case 0:
				return &v.state
			case 1:
				return &v.sizeCache
			case 2:
				return &v.unknownFields
			default:
				return nil
			}
		}
		file_yt_yt_proto_yt_client_discovery_client_proto_discovery_client_service_proto_msgTypes[8].Exporter = func(v interface{}, i int) interface{} {
			switch v := v.(*TReqGetGroupMeta); i {
			case 0:
				return &v.state
			case 1:
				return &v.sizeCache
			case 2:
				return &v.unknownFields
			default:
				return nil
			}
		}
		file_yt_yt_proto_yt_client_discovery_client_proto_discovery_client_service_proto_msgTypes[9].Exporter = func(v interface{}, i int) interface{} {
			switch v := v.(*TRspGetGroupMeta); i {
			case 0:
				return &v.state
			case 1:
				return &v.sizeCache
			case 2:
				return &v.unknownFields
			default:
				return nil
			}
		}
		file_yt_yt_proto_yt_client_discovery_client_proto_discovery_client_service_proto_msgTypes[10].Exporter = func(v interface{}, i int) interface{} {
			switch v := v.(*TReqHeartbeat); i {
			case 0:
				return &v.state
			case 1:
				return &v.sizeCache
			case 2:
				return &v.unknownFields
			default:
				return nil
			}
		}
		file_yt_yt_proto_yt_client_discovery_client_proto_discovery_client_service_proto_msgTypes[11].Exporter = func(v interface{}, i int) interface{} {
			switch v := v.(*TRspHeartbeat); i {
			case 0:
				return &v.state
			case 1:
				return &v.sizeCache
			case 2:
				return &v.unknownFields
			default:
				return nil
			}
		}
	}
	type x struct{}
	out := protoimpl.TypeBuilder{
		File: protoimpl.DescBuilder{
			GoPackagePath: reflect.TypeOf(x{}).PkgPath(),
			RawDescriptor: file_yt_yt_proto_yt_client_discovery_client_proto_discovery_client_service_proto_rawDesc,
			NumEnums:      0,
			NumMessages:   12,
			NumExtensions: 0,
			NumServices:   0,
		},
		GoTypes:           file_yt_yt_proto_yt_client_discovery_client_proto_discovery_client_service_proto_goTypes,
		DependencyIndexes: file_yt_yt_proto_yt_client_discovery_client_proto_discovery_client_service_proto_depIdxs,
		MessageInfos:      file_yt_yt_proto_yt_client_discovery_client_proto_discovery_client_service_proto_msgTypes,
	}.Build()
	File_yt_yt_proto_yt_client_discovery_client_proto_discovery_client_service_proto = out.File
	file_yt_yt_proto_yt_client_discovery_client_proto_discovery_client_service_proto_rawDesc = nil
	file_yt_yt_proto_yt_client_discovery_client_proto_discovery_client_service_proto_goTypes = nil
	file_yt_yt_proto_yt_client_discovery_client_proto_discovery_client_service_proto_depIdxs = nil
}
