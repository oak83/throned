package main

import (
	"ThroneCore/gen"
	"ThroneCore/internal/boxdns"
	"ThroneCore/internal/killswitch"
	"context"
	"errors"
)

func (s *server) GetDefaultInterface(ctx context.Context, in *gen.EmptyReq) (*gen.GetDefaultInterfaceResponse, error) {
	ifc := boxdns.DefaultInterface()
	if ifc == nil {
		return nil, errors.New("no default interface")
	}
	return &gen.GetDefaultInterfaceResponse{
		Name:  To(ifc.Name),
		Index: To(int32(ifc.Index)),
	}, nil
}

func (s *server) SetTransitionGuard(ctx context.Context, in *gen.SetTransitionGuardRequest) (*gen.ErrorResp, error) {
	if in.GetEnabled() {
		if err := killswitch.Enable(); err != nil {
			msg := err.Error()
			return &gen.ErrorResp{Error: &msg}, nil
		}
	} else {
		killswitch.Disable()
	}
	return &gen.ErrorResp{}, nil
}
